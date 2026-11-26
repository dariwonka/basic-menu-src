#include "Engine.h"
#include "Offsets.h"
#include "Vehicles.h"
#include "Log.h"
#include <kernel.h>
#include <np/np_common.h>
#include <user_service/user_service_api.h>
#include <atomic>
#include <string.h>

using namespace Offsets;

namespace Engine {

namespace {
	typedef void (*Void_t)();
	typedef void (*SetTime_t)(int, int, int);
	typedef void (*Spawn_t)(uint64_t);
	typedef uint8_t* (*GetComp_t)(uint32_t);
	typedef void (*SetWanted_t)(uint8_t*, int, int, int, char);
	typedef void (*Handle_t)(uint32_t);
	typedef void (*Object_t)(uint8_t*);
	typedef void (*StreamRequest_t)(uint16_t*, uint32_t*, void*, int, int, int, int, void*);
	typedef bool (*StreamReady_t)(uint16_t*);
	typedef void (*StreamRelease_t)(uint16_t*);

	uint64_t s_base = 0;
	bool s_versionOk = false;
	const char* s_reason = "not started";
	std::atomic<bool> s_online{false};

	struct Action { Op op; int64_t arg; };
	const uint32_t kQueue = 32;
	Action s_queue[kQueue];
	std::atomic<uint32_t> s_head{0}, s_tail{0};

	std::atomic<bool> s_neverWanted{false}, s_freezeTime{false};
	bool s_freezeArmed = false;
	int s_frozenH = 12, s_frozenM = 0;
	uint64_t s_lastWantedMs = 0, s_lastNpMs = 0;

	struct Spawn { bool active; uint64_t hash; uint16_t req; uint32_t model; uint64_t startMs; const char* name; };
	Spawn s_spawn = {};
	uint8_t s_aux[16];

	Status s_status = { 0, 0, -1, -1, -1 };

	template <typename T> T Fn(uint64_t offset) { return (T)(s_base + offset); }
	template <typename T> T& Var(uint64_t offset) { return *(T*)(s_base + offset); }
	uint64_t NowMs() { return sceKernelGetProcessTime() / 1000; }

	bool Readable(uint64_t addr, uint64_t len)
	{
		SceKernelVirtualQueryInfo info;
		if (sceKernelVirtualQuery((void*)addr, 0, &info, sizeof(info)) < 0) return false;
		return info.isCommitted && (info.protection & SCE_KERNEL_PROT_CPU_READ) &&
			(uint64_t)info.start <= addr && (uint64_t)info.end >= addr + len;
	}

	// story mode only: locked while PSN is reachable
	void CheckOnline()
	{
		SceUserServiceUserId user = -1;
		if (sceUserServiceGetInitialUser(&user) != 0) return;
		SceNpState state = SCE_NP_STATE_UNKNOWN;
		SceNpReachabilityState reach = SCE_NP_REACHABILITY_STATE_UNAVAILABLE;
		bool online = sceNpGetState(user, &state) == 0 && state == SCE_NP_STATE_SIGNED_IN &&
			sceNpGetNpReachabilityState(user, &reach) == 0 && reach == SCE_NP_REACHABILITY_STATE_REACHABLE;
		if (online != s_online.load()) Log("online: %d", online);
		s_online.store(online);
	}

	uint8_t* PlayerPed() { return *(uint8_t* volatile*)(s_base + LocalPlayerPed); }
	uint32_t HandleOf(uint8_t* ped) { return *(uint32_t*)(ped + 0x9C); }

	int ReadWanted(uint8_t* ped)
	{
		if (!ped) return -1;
		uint32_t h = HandleOf(ped);
		if (((h >> 17) & 0x1F) != 3) return -1;
		uint8_t* comp = Fn<GetComp_t>(GetPlayerComp)(h);
		return comp ? *(int*)(comp + WantedLevel) : -1;
	}

	bool LookupModel(uint64_t hash, uint32_t* model)
	{
		uint64_t map = s_base + ArchetypeMap;
		uint32_t buckets = *(volatile uint32_t*)(map + 4);
		if (!buckets) return false;
		uint64_t b = map + 8 + (hash % buckets) * 0x58;

		uint32_t n = *(uint32_t*)(b + 4);
		uint64_t* keys = *(uint64_t**)(b + 8);
		uint32_t* vals = *(uint32_t**)(b + 0x18);
		uint32_t v = 0;
		bool found = false;
		if (n && keys && vals && Readable((uint64_t)keys, 8ull * n) && Readable((uint64_t)vals, 4ull * n))
			for (uint32_t i = 0; i < n && !found; i++)
				if (keys[i] == hash) { v = vals[i]; found = true; }

		if (!found) {
			uint8_t* arr = *(uint8_t**)(b + 0x48);
			uint16_t cnt = *(uint16_t*)(b + 0x50);
			if (arr && cnt && Readable((uint64_t)arr, 16ull * cnt))
				for (uint32_t i = 0; i < cnt && !found; i++)
					if (*(uint64_t*)(arr + 16 * i) == hash) { v = *(uint32_t*)(arr + 16 * i + 8); found = true; }
		}
		if (!found) return false;

		uint32_t ex = *(uint32_t*)(b + 0x24);
		uint32_t* exl = *(uint32_t**)(b + 0x28);
		if (ex && exl && Readable((uint64_t)exl, 4ull * ex))
			for (uint32_t i = 0; i < ex; i++)
				if (((v ^ exl[i]) & 0x3FFFFF) == 0) return false;
		*model = v;
		return true;
	}

	void StartSpawn(int index)
	{
		if (index < 0 || index >= kVehicleModelCount || s_spawn.active) return;
		const VehicleModel& vm = kVehicleModels[index];
		uint32_t model = 0;
		if (!LookupModel(vm.hash, &model)) { Log("spawn %s: unknown model", vm.name); return; }
		memset(s_aux, 0, sizeof(s_aux));
		s_spawn = { true, vm.hash, 0, model, NowMs(), vm.name };
		Fn<StreamRequest_t>(StreamRequest)(&s_spawn.req, &s_spawn.model, (void*)(s_base + StreamCtx), 0, 9, 1, 0, s_aux);
	}

	void StepSpawn(uint64_t now)
	{
		if (!s_spawn.active) return;
		bool loaded = (s_spawn.req & 0x3FFF) && Fn<StreamReady_t>(StreamReadyA)(&s_spawn.req) &&
			Fn<StreamReady_t>(StreamReadyB)(&s_spawn.req);
		if (loaded) {
			Var<volatile uint32_t>(LastCheatVeh) = 0x1FFFF;   // keep the previous vehicle
			Fn<Spawn_t>(SpawnVehicle)(s_spawn.hash);
			Log("spawned %s", s_spawn.name);
		} else if (now - s_spawn.startMs < 6000) {
			return;
		} else {
			Log("spawn %s: timed out", s_spawn.name);
		}
		if (s_spawn.req & 0x3FFF) Fn<StreamRelease_t>(StreamRelease)(&s_spawn.req);
		s_spawn.active = false;
	}

	void Run(const Action& a, uint8_t* ped)
	{
		switch (a.op) {
		case Op::HealRepair: {
			Fn<Void_t>(CheatHealRepair)();
			Fn<Handle_t>(HealthRestore)(HandleOf(ped));
			uint8_t* veh = Var<uint8_t* volatile>(LocalPlayerVeh);
			if (veh) Fn<Object_t>(VehicleFix)(veh);
			break;
		}
		case Op::SetWanted:    Fn<SetWanted_t>(SetPlayerWanted)(ped, (int)a.arg, 1, 1, 1); break;
		case Op::WantedUp:     Fn<Void_t>(CheatWantedUp)(); break;
		case Op::WantedDown:   Fn<Void_t>(CheatWantedDown)(); break;
		case Op::NextWeather:  Fn<Void_t>(CheatWeather)(); break;
		case Op::SetHour:
			Fn<SetTime_t>(ClockSetTime)((int)a.arg, 0, 0);
			s_frozenH = (int)a.arg;
			s_frozenM = 0;
			break;
		case Op::SpawnVehicle: StartSpawn((int)a.arg); break;
		}
	}
}

bool Init(uint64_t base, uint64_t end)
{
	s_base = base;
	if (end - base != ImageSize) {
		s_reason = "unsupported game version";
		Log("image size 0x%llx, expected 0x%llx", (unsigned long long)(end - base), (unsigned long long)ImageSize);
		return false;
	}
	uint64_t table = base + CheatTable;
	if (!Readable(table, 16 * 8)) { s_reason = "unsupported game version"; return false; }
	const uint64_t* t = (const uint64_t*)table;
	if (t[2] != base + CheatHealRepair || t[3] != base + CheatWantedUp ||
		t[4] != base + CheatWantedDown || t[6] != base + CheatWeather) {
		s_reason = "unsupported game version";
		Log("cheat table mismatch");
		return false;
	}
	CheckOnline();
	s_versionOk = true;
	s_reason = nullptr;
	return true;
}

bool Ready() { return s_versionOk && !s_online.load(); }

const char* LockReason()
{
	if (!s_versionOk) return s_reason;
	if (s_online.load()) return "online";
	return nullptr;
}

void Queue(Op op, int64_t arg)
{
	if (!Ready()) return;
	uint32_t h = s_head.load();
	if (h - s_tail.load() >= kQueue) return;
	s_queue[h % kQueue] = { op, arg };
	s_head.store(h + 1);
}

void SetNeverWanted(bool on) { s_neverWanted.store(on); }

void SetFreezeTime(bool on)
{
	s_freezeArmed = false;
	s_freezeTime.store(on);
}

void GameTick()
{
	if (!s_versionOk) return;
	uint64_t now = NowMs();
	if (now - s_lastNpMs > 5000) { s_lastNpMs = now; CheckOnline(); }
	if (s_online.load()) { s_tail.store(s_head.load()); return; }

	uint8_t* ped = PlayerPed();
	int* clock = (int*)(s_base + GameClock);
	s_status.ped = (uint64_t)ped;
	s_status.handle = ped ? HandleOf(ped) : 0;
	s_status.wanted = ReadWanted(ped);
	s_status.hour = clock[4];
	s_status.minute = clock[5];
	if (!ped) return;

	while (s_tail.load() != s_head.load()) {
		uint32_t t = s_tail.load();
		Run(s_queue[t % kQueue], ped);
		s_tail.store(t + 1);
	}

	if (s_neverWanted.load() && s_status.wanted > 0 && now - s_lastWantedMs > 1000) {
		s_lastWantedMs = now;
		Fn<SetWanted_t>(SetPlayerWanted)(ped, 0, 1, 1, 1);
	}

	StepSpawn(now);

	if (s_freezeTime.load()) {
		if (!s_freezeArmed) { s_frozenH = clock[4]; s_frozenM = clock[5]; s_freezeArmed = true; }
		Fn<SetTime_t>(ClockSetTime)(s_frozenH, s_frozenM, 0);
	}
}

Status GetStatus() { return s_status; }

}
