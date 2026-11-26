#include "Extras.h"
#include "Vehicles.h"
#include "Locations.h"
#include "Engine.h"
#include "Log.h"
#include <kernel.h>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

namespace Extras {

namespace {
	// static addresses (image base 0)
	const uint64_t kLock            = 0x1050;      // (void* lock)
	const uint64_t kUnlock          = 0x1000;      // (void* lock)
	const uint64_t kTextLock        = 0xE76B330;
	const uint64_t kTextFind        = 0x1F6B340;   // (u64* key, u32 hash, int, int) -> entry*
	const uint64_t kArchMap         = 0xE561800;
	const uint64_t kArchArray       = 0xE758058;
	const uint64_t kArchStride      = 0xE758060;
	const uint64_t kArchValid       = 0xE758078;
	const uint64_t kWeatherCount    = 0xD796A00;
	const uint64_t kWeatherTypes    = 0xD796A38;   // stride 0x38, hash at +0
	const uint64_t kWeatherSetNow   = 0x14F2790;   // (idx, 0)
	const uint64_t kGetInventory    = 0x1ADFF60;   // (u32 handle) -> comp*, inventory at +0x18
	const uint64_t kCurrencyAmount  = 0x1ADDB30;   // (inv*, u32 currency, u32 location) -> i64
	const uint64_t kLocationDb      = 0xDCEDB10;
	const uint64_t kIterKey         = 0xDCDB6B8;
	const uint64_t kDefaultItemHash = 0x9C7A2E8;
	const uint64_t kIterBegin       = 0x20D4040;   // (it*, inv*, u32* key, 0)
	const uint64_t kIterVisit       = 0x56B3EB0;   // (it*, entry+8, functor*)
	const uint64_t kIterEnd         = 0x20D4430;   // (it*)
	const uint64_t kVisitAdapter    = 0x56B4B80;
	const uint64_t kFunctorManager  = 0x836050;
	const uint64_t kFunctorNop      = 0x12D0;
	const uint64_t kOutfitSet       = 0x5C76B80;   // () -> u32 set for the current character
	const uint64_t kOutfitRequest   = 0x29A9580;   // (u32* out, outfit, variant, set)
	const uint64_t kOutfitPending   = 0x37764F0;   // (u64 req, u32 id) -> bool
	const uint64_t kOutfitApply     = 0x29AC4B0;   // (ped*, u32 handle, 2) -> bool
	const uint64_t kOutfitSlots     = 0xB106568;   // count u32 at +4, 0x80-byte slots at +8
	const uint64_t kOutfitLock      = 0xB0F8B50;
	const uint64_t kOutfitInvalid   = 0xC31DDF0;
	const uint64_t kWardrobe        = 0xE55FE08;   // u16 count at +8
	const uint64_t kCharacter       = 0xDCECDE8;   // ptr, character key u32 at +0x40
	const uint64_t kVehExtras       = 0x747AA0;    // (u32 handle) -> comp*, 12 x 14 bytes at +0x3D40
	const uint64_t kLocalPlayerVeh  = 0xDCEBCE0;
	const uint64_t kScriptLock      = 0xE6B6F10;

	const uint64_t kExplode         = 0x82538D0;   // (args*, 1, 6)
	const uint64_t kPedClone        = 0x53A8D20;   // (ped*, 1, 0, 0, 0, 33, F, F) -> handle or -1
	const uint64_t kFunctorCopy     = 0x47CB90;

	const uint32_t kCash = 0x7C407083, kOnHand = 0x96F2DE61, kAnyLocation = 0xA1212100;

	uint64_t s_base = 0;
	template <typename T> T Fn(uint64_t a) { return (T)(s_base + a); }
	void* At(uint64_t a) { return (void*)(s_base + a); }

	bool Readable(const void* p, uint64_t len)
	{
		SceKernelVirtualQueryInfo info;
		if (!p || sceKernelVirtualQuery(p, 0, &info, sizeof(info)) < 0) return false;
		return info.isCommitted && (info.protection & SCE_KERNEL_PROT_CPU_READ) &&
			(uint64_t)info.start <= (uint64_t)p && (uint64_t)info.end >= (uint64_t)p + len;
	}

	uint32_t Joaat(const char* s)
	{
		uint32_t h = 0;
		for (; *s; s++) {
			char c = *s;
			if (c >= 'A' && c <= 'Z') c += 32;
			h += (uint8_t)c; h += h << 10; h ^= h >> 6;
		}
		h += h << 3; h ^= h >> 11; h += h << 15;
		return h;
	}

	char s_result[64] = "";
	void Result(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
	void Result(const char* fmt, ...)
	{
		va_list a; va_start(a, fmt); vsnprintf(s_result, sizeof(s_result), fmt, a); va_end(a);
		Log("%s", s_result);
	}

	// ---- text ----
	bool LabelText(uint32_t hash, char* dst, size_t cap)
	{
		dst[0] = 0;
		if (!hash) return false;
		Fn<void (*)(void*)>(kLock)(At(kTextLock));
		uint64_t key = 0xFFFFFFFF00000000ull;
		uint8_t* e = Fn<uint8_t* (*)(uint64_t*, uint32_t, int, int)>(kTextFind)(&key, hash, 0, 0);
		if (e) {
			alignas(16) uint8_t out[0x20] = {};
			typedef bool (*Get_t)(void*, int, void*);
			Get_t get = *(Get_t*)(*(uint8_t**)e + 0x10);
			if (get(e, 0, out)) {
				const char* s = *(const char**)(out + 0x18);
				size_t n = 0;
				for (; s && *s && n + 1 < cap; s++) {
					if (*s == '~') { s++; while (*s && *s != '~') s++; if (!*s) break; continue; }
					dst[n++] = *s;
				}
				dst[n] = 0;
			}
		}
		Fn<void (*)(void*)>(kUnlock)(At(kTextLock));
		return dst[0] != 0;
	}

	// ---- vehicle names ----
	char s_vehLabel[kVehicleModelCount][40];
	bool s_vehLabelsDone = false;
	uint64_t s_lastLabelTryMs = 0;

	uint8_t* Archetype(uint64_t hash)
	{
		uint8_t* map = (uint8_t*)At(kArchMap);
		uint32_t buckets = *(volatile uint32_t*)(map + 4);
		if (!buckets) return nullptr;
		uint8_t* b = map + 8 + (hash % buckets) * 0x58;
		uint32_t n = *(uint32_t*)(b + 4);
		uint64_t* keys = *(uint64_t**)(b + 8);
		uint32_t* vals = *(uint32_t**)(b + 0x18);
		if (!n || !Readable(keys, 8ull * n) || !Readable(vals, 4ull * n)) return nullptr;
		uint32_t v = 0xFFFFFFFF;
		for (uint32_t i = 0; i < n; i++) if (keys[i] == hash) { v = vals[i]; break; }
		if (v == 0xFFFFFFFF) return nullptr;
		uint32_t slot = v & 0x3FFFFF;
		uint8_t* valid = *(uint8_t**)At(kArchValid);
		uint8_t* arr = *(uint8_t**)At(kArchArray);
		uint32_t stride = *(uint32_t*)At(kArchStride);
		if (!valid || !arr || !Readable(valid + (slot >> 3), 1) || !(valid[slot >> 3] & (1 << (slot & 7)))) return nullptr;
		uint8_t* arch = *(uint8_t**)(arr + (uint64_t)slot * stride);
		return Readable(arch, 0x200) ? arch : nullptr;
	}

	void ResolveVehicleLabels()
	{
		int named = 0;
		for (int i = 0; i < kVehicleModelCount; i++) {
			uint8_t* arch = Archetype(kVehicleModels[i].hash);
			if (!arch || (*(uint32_t*)(arch + 153) & 7) != 5) continue;
			if (LabelText(*(uint32_t*)(arch + 288), s_vehLabel[i], sizeof(s_vehLabel[i]))) named++;
		}
		Log("vehicle labels resolved %d/%d (e.g. %s -> '%s')", named, kVehicleModelCount,
			kVehicleModels[0].name, s_vehLabel[0]);
		s_vehLabelsDone = named > 0;
	}

	// ---- weather ----
	const char* kWeatherNames[] = { "sunny", "clear", "extrasunny", "clouds", "cloudy", "overcast", "rain", "drizzle",
		"clearing", "thunder", "thunderstorm", "storm", "smog", "foggy", "fog", "misty", "haze", "hail", "hurricane",
		"tropical_storm", "snow", "snowlight", "blizzard", "xmas", "halloween", "neutral", "humid", "heatwave",
		"sandstorm", "windy", "lightning", "sunset", "sunrise", "night", "rainbow" };
	const int kWeatherNameCount = sizeof(kWeatherNames) / sizeof(kWeatherNames[0]);
	uint32_t s_weatherNameHash[kWeatherNameCount];
	uint32_t s_weather[256];
	std::atomic<int> s_weatherCount{0};

	void LoadWeather()
	{
		if (s_weatherCount.load()) return;
		uint32_t n = *(volatile uint32_t*)At(kWeatherCount);
		uint8_t* types = *(uint8_t**)At(kWeatherTypes);
		if (!n || n > 256 || !Readable(types, 0x38ull * n)) return;
		for (uint32_t i = 0; i < n; i++) s_weather[i] = *(uint32_t*)(types + 0x38 * i);
		s_weatherCount.store((int)n);
		Log("%u weather types", n);
	}

	void SetWeather(int idx)
	{
		uint8_t* b = (uint8_t*)s_base;
		*(uint32_t*)(b + 0xD796D40) = 0; *(uint8_t*)(b + 0xD796D46) = 0; *(uint32_t*)(b + 0xD796D48) = 0;
		*(uint64_t*)(b + 0xD796D68) = ~0ull; *(uint32_t*)(b + 0xD796D78) = 0; *(uint64_t*)(b + 0xD796D70) = 0;
		Fn<void (*)(int, int)>(kWeatherSetNow)(idx, 0);
		*(uint32_t*)(b + 0xD796D10) = idx;
		*(uint32_t*)(b + 0xD796D14) = idx;
		*(uint32_t*)(b + 0xD796D4C) = idx;
		*(uint32_t*)(b + 0xD796D3C) = 0; *(uint32_t*)(b + 0xD796D30) = 0xFFFFFFFF; *(uint64_t*)(b + 0xD796D34) = 0;
		*(uint16_t*)(b + 0xD796D44) = 256; *(uint8_t*)(b + 0xD796D2C) = 0;
		Result("weather %d (%08x)", idx, s_weather[idx]);
	}

	// ---- cash / item stacks ----
	std::atomic<int64_t> s_cash{-1}, s_stash{-1};
	uint64_t s_lastCashMs = 0;
	uint8_t* s_hit = nullptr;
	uint32_t s_want = 0;

	uint8_t* Inventory(uint8_t* ped)
	{
		uint8_t* comp = Fn<uint8_t* (*)(uint32_t)>(kGetInventory)(*(uint32_t*)(ped + 0x9C));
		return comp ? *(uint8_t**)(comp + 0x18) : nullptr;
	}

	int StackVisitor(void**, uint8_t* group, uint32_t loc)
	{
		uint8_t** items = *(uint8_t***)(group + 8);
		uint16_t n = *(uint16_t*)(group + 0x10);
		for (int i = 0; i < n && !s_hit; i++) {
			uint8_t* it = items[i];
			uint8_t* def = *(uint8_t**)(it + 0x38);
			uint32_t h = def ? *(uint32_t*)(def + 8) : *(uint32_t*)At(kDefaultItemHash);
			if (h == s_want && (loc == kAnyLocation || *(uint32_t*)(it + 0x40) == loc)) s_hit = it;
		}
		return 1;
	}

	uint8_t* LocationEntry(uint32_t location)
	{
		uint8_t* db = *(uint8_t**)At(kLocationDb);
		if (!db) return nullptr;
		uint8_t* e = *(uint8_t**)(db + 0xB0);
		uint16_t n = *(uint16_t*)(db + 0xB8);
		if (!e || !Readable(e, 32ull * n)) return nullptr;
		for (uint16_t i = 0; i < n; i++) if (*(uint32_t*)(e + 32 * i) == location) return e + 32 * i;
		return nullptr;
	}

	uint8_t* FindStack(uint8_t* inv, uint32_t item, uint32_t location)
	{
		uint8_t* E = LocationEntry(location);
		if (!E) return nullptr;
		alignas(32) uint8_t it[0x80] = {}, inner[0x80] = {}, outer[0x80] = {};
		uint32_t k = *(uint32_t*)At(kIterKey);
		s_want = item; s_hit = nullptr;
		*(void**)(inner + 0x50) = (void*)StackVisitor;
		*(void**)(inner + 0x58) = At(kFunctorManager);
		*(void**)(inner + 0x60) = At(kFunctorNop);
		void** oc = (void**)(((uintptr_t)outer + 0xF) & ~0xFull);
		oc[0] = inv; oc[1] = inner;
		*(void**)(outer + 0x50) = At(kVisitAdapter);
		*(void**)(outer + 0x58) = At(kFunctorManager);
		*(void**)(outer + 0x60) = At(kFunctorNop);
		Fn<void (*)(void*, void*, void*, int)>(kIterBegin)(it, inv, &k, 0);
		Fn<void (*)(void*, void*, void*)>(kIterVisit)(it, E + 8, outer);
		Fn<void (*)(void*)>(kIterEnd)(it);
		return s_hit;
	}

	void ReadCash(uint8_t* ped)
	{
		uint8_t* inv = Inventory(ped);
		if (inv) { s_cash.store(Fn<int64_t (*)()>(0x3BA94C0)()); s_stash.store(Fn<int64_t (*)()>(0x3BA9500)()); }
	}

	void AddCash(uint8_t* ped, int64_t amount)
	{
		uint8_t* inv = Inventory(ped);
		if (!inv) { Result("cash: no inventory"); return; }
		uint8_t* entryHand = LocationEntry(kOnHand);
		uint8_t* entryAny = LocationEntry(kAnyLocation);
		uint8_t* stack = FindStack(inv, kCash, kOnHand);
		if (!stack) stack = FindStack(inv, kCash, kAnyLocation);
		Log("cash: inv %p, location entries hand %p any %p, reads %lld, stack %p", inv, entryHand, entryAny,
			(long long)s_cash.load(), stack);
		if (!stack) { Result("cash: no cash stack found"); return; }
		int64_t before = *(int64_t*)(stack + 0x18);
		*(int64_t*)(stack + 0x18) = before + amount;
		ReadCash(ped);
		Result("cash %lld -> %lld (reads %lld)", (long long)before, (long long)(before + amount), (long long)s_cash.load());
	}

	// ---- outfits ----
	struct Outfit { uint32_t set, outfit, variant; };
	Outfit s_outfits[1024];
	std::atomic<int> s_outfitCount{0};
	uint32_t s_outfitHandle = 0;
	bool s_outfitPending = false;
	uint64_t s_outfitStartMs = 0;

	void ListOutfits()
	{
		uint8_t* chr = *(uint8_t**)At(kCharacter);
		uint8_t* l1 = *(uint8_t**)At(kWardrobe);
		uint16_t n1 = *(uint16_t*)At(kWardrobe + 8);
		if (!chr || !l1 || !Readable(l1, 56ull * n1)) { Result("outfits: wardrobe not readable"); return; }
		uint32_t who = *(uint32_t*)(chr + 0x40);
		int count = 0;
		for (uint16_t i = 0; i < n1; i++) {
			uint8_t* c = l1 + 56 * i;
			if (*(uint32_t*)c != who) continue;
			uint8_t* sets = *(uint8_t**)(c + 0x10); uint16_t ns = *(uint16_t*)(c + 0x18);
			for (uint16_t s = 0; sets && s < ns && Readable(sets + 32 * s, 32); s++) {
				uint8_t* outs = *(uint8_t**)(sets + 32 * s + 0x10); uint16_t no = *(uint16_t*)(sets + 32 * s + 0x18);
				for (uint16_t o = 0; outs && o < no && Readable(outs + 32 * o, 32); o++) {
					uint8_t* vars = *(uint8_t**)(outs + 32 * o + 0x10); uint16_t nv = *(uint16_t*)(outs + 32 * o + 0x18);
					for (uint16_t v = 0; vars && v < nv && Readable(vars + 40 * v, 40) && count < 1024; v++)
						if (*(uint16_t*)(vars + 40 * v + 0x18))
							s_outfits[count++] = { *(uint32_t*)(sets + 32 * s), *(uint32_t*)(outs + 32 * o), *(uint32_t*)(vars + 40 * v) };
				}
			}
		}
		s_outfitCount.store(count);
		Result("outfits: %d for character %08x", count, who);
		for (int i = 0; i < count && i < 6; i++) Log("outfit %d: set %08x outfit %08x variant %08x", i, s_outfits[i].set, s_outfits[i].outfit, s_outfits[i].variant);
	}

	struct F32 { uint64_t a, b, c, d; };
	const F32 kNoFunctor = { 0, 0, 0, 0 };

	uint8_t* OutfitSlot(uint32_t h) { return (uint8_t*)At(kOutfitSlots + 8 + (uint64_t)(h & 0xFFFF) * 0x80); }
	bool OutfitSlotOk(uint32_t h)
	{
		return (h & 0xFFFF) < *(uint32_t*)At(kOutfitSlots + 4) && *(uint16_t*)(OutfitSlot(h) + 0x78) == (h >> 16);
	}

	// open the slot, merge `src`'s pieces (mode), finalize + start streaming. Game thread.
	bool PrepareOutfit(uint32_t h, uint8_t* src, int mode)
	{
		Fn<void (*)(void*)>(kLock)(At(kOutfitLock));
		bool open = OutfitSlotOk(h) && !OutfitSlot(h)[0x7C];
		if (open) OutfitSlot(h)[0x7B] = 1;
		Fn<void (*)(void*)>(kUnlock)(At(kOutfitLock));
		if (!open) { Result("outfit: slot not usable"); return false; }
		Fn<void (*)(void*)>(kLock)(At(kScriptLock));
		Fn<char (*)(void*, uint32_t, int)>(0x29AC4B0)(src, h, mode);
		uint32_t ok = Fn<uint32_t (*)(uint32_t, uint32_t, F32, F32)>(0x5C48470)(h, 1, kNoFunctor, kNoFunctor);
		Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
		uint8_t* S = OutfitSlot(h);
		Log("outfit %08x finalize %u, S+0 %llx S+8 %08x", h, ok, (unsigned long long)*(uint64_t*)S, *(uint32_t*)(S + 8));
		if (!ok) { Fn<void (*)(uint32_t)>(0x29ABD90)(h); Result("outfit: finalize failed"); return false; }
		s_outfitHandle = h; s_outfitPending = true; s_outfitStartMs = sceKernelGetProcessTime() / 1000;
		return true;
	}

	void StartOutfit(uint8_t* ped, int idx)
	{
		if (idx < 0 || idx >= s_outfitCount.load()) return;
		Fn<void (*)(void*, uint32_t, bool)>(0x1FD29E0)(ped, 1041, false);
		uint32_t set = s_outfits[idx].set ? s_outfits[idx].set : Fn<uint32_t (*)()>(kOutfitSet)();
		uint32_t h = 0;
		Fn<void (*)(void*)>(kLock)(At(kScriptLock));
		Fn<void (*)(uint32_t*, uint32_t, uint32_t, uint32_t)>(kOutfitRequest)(&h, s_outfits[idx].outfit, s_outfits[idx].variant, set);
		Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
		if (h == *(uint32_t*)At(kOutfitInvalid)) { Result("outfit %d: request refused", idx); return; }
		PrepareOutfit(h, ped, 0);   // keep hair / skin / head
	}

	uint8_t* NearestPed(uint8_t* ped);

	void CopyNearestLook(uint8_t* ped)
	{
		uint8_t* src = NearestPed(ped);
		if (!src) { Result("no ped nearby"); return; }
		uint32_t h = 0; uint8_t reused = 0;
		Fn<void (*)(uint32_t*, uint8_t*, uint8_t)>(0x29A9870)(&h, &reused, 2);
		if (h == *(uint32_t*)At(kOutfitInvalid)) { Result("copy look: no free slot"); return; }
		Fn<void (*)(void*, uint32_t, bool)>(0x1FD29E0)(ped, 1041, false);
		PrepareOutfit(h, src, 2);
	}

	void StepOutfit(uint8_t* ped, uint64_t now)
	{
		if (!s_outfitPending) return;
		uint32_t h = s_outfitHandle;
		Fn<void (*)(void*)>(kLock)(At(kOutfitLock));
		uint8_t* S = OutfitSlot(h);
		bool loaded = OutfitSlotOk(h) && *(uint64_t*)S &&
			!Fn<bool (*)(uint64_t, uint32_t)>(kOutfitPending)(*(uint64_t*)S, *(uint32_t*)(S + 8));
		Fn<void (*)(void*)>(kUnlock)(At(kOutfitLock));
		if (loaded) {
			Fn<void (*)(void*)>(kLock)(At(kScriptLock));
			uint32_t applied = Fn<uint32_t (*)(void*, uint32_t, F32, F32)>(0x5C38EE0)(ped, h, kNoFunctor, kNoFunctor);
			Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
			Fn<void (*)(uint32_t)>(0x29ABD90)(h);
			Result("outfit applied: %u", applied);
			s_outfitPending = false;
		} else if ((int64_t)(sceKernelGetProcessTime() / 1000 - s_outfitStartMs) > 8000) {
			Fn<void (*)(uint32_t)>(0x29ABD90)(h);
			Result("outfit: timed out loading");
			s_outfitPending = false;
		}
	}

	// ---- extras ----
	void SetExtra(int idx, bool on)
	{
		uint8_t* veh = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		if (!veh || idx < 0 || idx > 11) { Result("extra: no vehicle"); return; }
		uint8_t* c = Fn<uint8_t* (*)(uint32_t)>(kVehExtras)(*(uint32_t*)(veh + 0x9C));
		if (!c) { Result("extra: vehicle has no extras"); return; }
		c[0x3D40 + 14 * idx] = on;
		c[0x3D41 + 14 * idx] = 1;
		Result("extra %d %s", idx + 1, on ? "on" : "off");
	}

	// ---- explosions / clone ----
	void EntPos(uint8_t* e, float* x, float* y, float* z)
	{
		*x = *(float*)(e + 0x70) + 32.0f * *(int16_t*)(e + 0x7C);
		*y = *(float*)(e + 0x74) + 32.0f * *(int16_t*)(e + 0x7E);
		*z = *(float*)(e + 0x78);
	}

	void ExplodeAt(float x, float y, float z, int type, bool log);

	void Explode(uint8_t* ped, int type)
	{
		float x, y, z;
		EntPos(ped, &x, &y, &z);
		const float* fwd = (const float*)(ped + 0x50);
		ExplodeAt(x + 10.0f * fwd[0], y + 10.0f * fwd[1], z, type, true);
	}

	void ExplodeAt(float x, float y, float z, int type, bool log)
	{
		alignas(16) uint8_t A[0x180] = {};
		*(uint32_t*)(A + 0x00) = (uint32_t)type;
		float* pos = (float*)(A + 0x10); pos[0] = x; pos[1] = y; pos[2] = z; pos[3] = 0;
		*(float*)(A + 0x3C) = 1.0f;
		*(float*)(A + 0x40) = 1.0f;
		*(float*)(A + 0x48) = 1.0f;
		*(float*)(A + 0x4C) = -1.0f;
		*(uint64_t*)(A + 0x50) = 0x100000000ull;
		A[0x54] = 1; A[0x55] = 0;
		float* up = (float*)(A + 0x60); up[0] = 0; up[1] = 0; up[2] = 1; up[3] = 0;
		*(uint64_t*)(A + 0x84) = 0xFFFFFFFFull;
		*(uint64_t*)(A + 0x90) = 0x27F25C0E00000000ull;
		*(uint32_t*)(A + 0xA8) = 0xFFFCFFFF;
		*(uint64_t*)(A + 0xB0) = 0xFFFFFFFF00000000ull;
		*(uint64_t*)(A + 0xB8) = ~0ull;
		*(uint32_t*)(A + 0xC0) = 0xFFFFFFFF; *(uint32_t*)(A + 0xC4) = 8;
		*(float*)(A + 0x10C) = 1.0f; *(float*)(A + 0x110) = 1.0f; *(float*)(A + 0x114) = 1.0f;
		A[0x128] = 0xFF; A[0x137] = 1;
		*(uint32_t*)(A + 0x13C) = 0xFFFFFFFF;
		Fn<void (*)(void*, int, int)>(kExplode)(A, 1, 6);
		static const int kSlots[] = { 0x20, 0x28, 0x30, 0x70 };
		for (int off : kSlots)
			if (*(void**)(A + off)) Fn<void (*)(void*, void*)>(0x1832910)(*(void**)(A + off), A + off);
		if (log) Result("explosion type %d at %.0f %.0f %.0f", type, x, y, z);
	}

	// walk an entity pool (pool* at 0xE799500 + 8*type): base +0, stride +8, capacity +0xC, gen* +0x20, mask +0x28
	int ExplodeAll(uint8_t* ped, bool vehicles)
	{
		uint8_t* pool = *(uint8_t**)At(0xE799500 + 8 * (vehicles ? 2 : 3));
		if (!pool || !Readable(pool, 0x30)) return 0;
		uint8_t* base = *(uint8_t**)pool;
		uint32_t stride = *(uint32_t*)(pool + 8), cap = *(uint32_t*)(pool + 0xC);
		uint32_t* gen = *(uint32_t**)(pool + 0x20);
		uint32_t mask = *(uint32_t*)(pool + 0x28);
		if (!base || !gen || !Readable(gen, 4ull * cap)) return 0;
		uint8_t* mine = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		int n = 0;
		for (uint32_t i = 0; i < cap && n < 40; i++) {
			if (mask & ~gen[i]) continue;
			uint8_t* e = base + (uint64_t)i * stride;
			if (e == ped || e == mine || !Readable(e, 0x80)) continue;
			float x, y, z;
			EntPos(e, &x, &y, &z);
			ExplodeAt(x, y, z, vehicles ? 7 : 1, false);
			n++;
		}
		return n;
	}

	uint32_t PoolCapacity(int type)
	{
		uint8_t* pool = *(uint8_t**)At(0xE799500 + 8 * type);
		return pool && Readable(pool, 0x30) ? *(uint32_t*)(pool + 0xC) : 0;
	}

	uint8_t* PoolObject(int type, uint32_t i)
	{
		uint8_t* pool = *(uint8_t**)At(0xE799500 + 8 * type);
		if (!pool) return nullptr;
		uint8_t* base = *(uint8_t**)pool;
		uint32_t stride = *(uint32_t*)(pool + 8), cap = *(uint32_t*)(pool + 0xC);
		uint32_t* gen = *(uint32_t**)(pool + 0x20);
		uint32_t mask = *(uint32_t*)(pool + 0x28);
		if (!base || !gen || i >= cap || (mask & ~gen[i])) return nullptr;
		return base + (uint64_t)i * stride;
	}

	float Dist2(uint8_t* a, uint8_t* b)
	{
		float ax, ay, az, bx, by, bz;
		EntPos(a, &ax, &ay, &az); EntPos(b, &bx, &by, &bz);
		return (ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz);
	}

	uint8_t* NearestPed(uint8_t* ped)
	{
		uint8_t* best = nullptr; float bestD = 50.0f * 50.0f;
		for (uint32_t i = 0, cap = PoolCapacity(3); i < cap; i++) {
			uint8_t* e = PoolObject(3, i);
			if (!e || e == ped || !Readable(e, 0xA0)) continue;
			float d = Dist2(e, ped);
			if (d < bestD) { bestD = d; best = e; }
		}
		return best;
	}

	// queue an entity on the engine's deferred-destroy ring (what the vehicle cheat uses for the previous car)
	bool QueueDelete(uint8_t* ent)
	{
		uint32_t h = *(uint32_t*)(ent + 0x9C);
		uint8_t* tc = Fn<uint8_t* (*)(uint32_t)>(0x22430)(h);
		if (tc && (__atomic_fetch_or((uint32_t*)(tc + 0x6A4), 1u, __ATOMIC_SEQ_CST) & 1)) return false;
		uint32_t cap = *(uint32_t*)At(0xCC5CD50);
		uint32_t* ctr = (uint32_t*)At(0xCC5CD60);
		uint64_t* buf = *(uint64_t**)At(0xCC5CD58);
		if (!cap || !buf) return false;
		uint32_t old, idx;
		do { old = *ctr; idx = (old + 1) % cap; } while (!__sync_bool_compare_and_swap(ctr, old, idx));
		uint8_t type = ent[0x30];
		uint8_t* pool = *(uint8_t**)At(0xE799500 + 8 * type);
		uint32_t slot = (uint32_t)(((uint64_t)ent - *(uint64_t*)pool) / *(uint32_t*)(pool + 8)) & 0x1FFFF;
		uint32_t gen = (*(uint32_t**)(pool + 0x20))[slot];
		uint32_t i = (idx - 1 < cap - 1) ? idx - 1 : cap - 1;
		buf[i] = ((uint64_t)((gen * 32) & 0xFFC00000)) | slot | ((uint64_t)(type & 0x1F) << 17);
		return true;
	}

	int DeleteNearby(uint8_t* ped, bool vehicles, bool peds)
	{
		uint8_t* mine = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		int n = 0;
		for (int type = 2; type <= 3; type++) {
			if ((type == 2 && !vehicles) || (type == 3 && !peds)) continue;
			for (uint32_t i = 0, cap = PoolCapacity(type); i < cap && n < 80; i++) {
				uint8_t* e = PoolObject(type, i);
				if (!e || e == ped || e == mine || !Readable(e, 0xA0)) continue;
				if (Dist2(e, ped) > 120.0f * 120.0f) continue;
				if (QueueDelete(e)) n++;
			}
		}
		return n;
	}

	void SetEngine(bool on)
	{
		uint8_t* veh = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		if (!veh) { Result("engine: no vehicle"); return; }
		Fn<void (*)(void*)>(kLock)(At(kScriptLock));
		Fn<void (*)(void*, int, int, int, float)>(0x27FB7C0)(veh, on, 1, on ? 0 : 1, -1.0f);
		Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
		Result("engine %s", on ? "on" : "off");
	}

	// ---- vehicle parts / paint / visibility ----
	void Door(int part, bool open)
	{
		uint8_t* veh = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		if (!veh) { Result("door: no vehicle"); return; }
		uint8_t* parts = Fn<uint8_t* (*)(uint32_t)>(0x11310)(*(uint32_t*)(veh + 0x9C));
		if (!parts || !Readable(parts, 0x38)) { Result("door: no part list"); return; }
		int n = *(int*)(parts + 0x24);
		uint8_t* list = *(uint8_t**)(parts + 0x30);
		if (n <= 0 || n > 64 || !list || !Readable(list, 0xC0ull * n)) { Result("door: bad part list"); return; }
		for (int i = 0; i < n; i++) {
			uint8_t* e = list + 0xC0 * i;
			if (*(int*)(e + 0x60) != part) continue;
			int zero = 0;
			Fn<void (*)(void*)>(kLock)(At(kScriptLock));
			Fn<void (*)(void*, int, int*, int, int, float)>(0x3618FF0)(e, open ? 2 : 0, &zero, 0, 1, 1.0f);
			Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
			Result("part %d %s", part, open ? "open" : "closed");
			return;
		}
		Result("part %d not on this vehicle (%d parts)", part, n);
	}

	void Paint(uint8_t r, uint8_t g, uint8_t b)
	{
		uint8_t* veh = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		if (!veh) { Result("paint: no vehicle"); return; }
		uint8_t* L = *(uint8_t**)(veh + 0x28);
		if (!L || !Readable(L, 0x30) || !(L[0x2E] & 0x10)) { Result("paint: no colour record"); return; }
		uint32_t n = *(uint32_t*)(L + 0x1C);
		uint8_t** arr = *(uint8_t***)(L + 0x20);
		if (!arr || n > 256 || !Readable(arr, 8ull * n)) { Result("paint: bad list"); return; }
		for (uint32_t i = 0; i < n; i++) {
			uint8_t* x = arr[i];
			if (!x || !Readable(x, 0x160) || *(uint32_t*)(x + 0x10) != 21) continue;
			*(float*)(x + 0x150) = r / 255.0f;
			*(float*)(x + 0x154) = g / 255.0f;
			*(float*)(x + 0x158) = b / 255.0f;
			if (!(x[0x18] & 2)) {
				uint8_t* y = *(uint8_t**)(x + 8);
				if (y && Readable(y, 0x18)) y[0x17] |= 0x80;
				x[0x18] |= 2;
			}
			Result("paint %u %u %u", r, g, b);
			return;
		}
		Result("paint: colour record not found");
	}

	void Invisible(uint8_t* ped, bool vehicle, bool hide)
	{
		uint8_t* ent = vehicle ? *(uint8_t* volatile*)At(kLocalPlayerVeh) : ped;
		if (!ent) { Result("invisible: no vehicle"); return; }
		Fn<void (*)(void*)>(kLock)(At(kScriptLock));
		if (hide) {
			uint8_t* c = Fn<uint8_t* (*)(void*)>(0x370B4F0)(ent);
			if (c) c[0x10] |= 0x10;
			if (vehicle) Fn<void (*)(void*, int, int, int)>(0x3102550)(ent, 0, 0, 0);
		} else {
			uint8_t* c = Fn<uint8_t* (*)(uint32_t)>(0xDA4EB0)(*(uint32_t*)(ent + 0x9C));
			if (c) c[0x10] &= ~0x10;
		}
		Fn<void (*)(void*)>(kUnlock)(At(kScriptLock));
		Result("%s %s", vehicle ? "vehicle" : "player", hide ? "hidden" : "visible");
	}

	// ---- fun ----
	bool s_meteors = false, s_blackHole = false;
	uint64_t s_lastMeteorMs = 0;
	uint32_t s_rand = 0x1234567;
	float Rand01() { s_rand = s_rand * 1664525u + 1013904223u; return (s_rand >> 8) / 16777216.0f; }

	void StepFun(uint8_t* ped, uint64_t now)
	{
		float px, py, pz;
		EntPos(ped, &px, &py, &pz);
		if (s_meteors && now - s_lastMeteorMs > 350) {
			s_lastMeteorMs = now;
			float a = Rand01() * 6.2832f, d = 12.0f + Rand01() * 35.0f;
			float c = 1.0f - a * a / 2 + a * a * a * a / 24, s = a - a * a * a / 6 + a * a * a * a * a / 120;   // rough cos/sin
			if (a > 3.1416f) { float b = a - 3.1416f; c = -(1.0f - b * b / 2 + b * b * b * b / 24); s = -(b - b * b * b / 6 + b * b * b * b * b / 120); }
			ExplodeAt(px + c * d, py + s * d, pz, (int)(Rand01() * 3) == 0 ? 7 : 1, false);
		}
		if (s_blackHole) {
			const float* f = (const float*)(ped + 0x50);
			float hx = px + f[0] * 25.0f, hy = py + f[1] * 25.0f, hz = pz + 8.0f;
			uint8_t* mine = *(uint8_t* volatile*)At(kLocalPlayerVeh);
			int n = 0;
			for (int type = 2; type <= 3 && n < 60; type++)
				for (uint32_t i = 0, cap = PoolCapacity(type); i < cap && n < 60; i++) {
					uint8_t* e = PoolObject(type, i);
					if (!e || e == ped || e == mine || !Readable(e, 0xA0)) continue;
					float ex, ey, ez;
					EntPos(e, &ex, &ey, &ez);
					float dx = hx - ex, dy = hy - ey, dz = hz - ez, d2 = dx * dx + dy * dy + dz * dz;
					if (d2 > 90.0f * 90.0f || d2 < 1.0f) continue;
					float k = 18.0f / (1.0f + d2 * 0.0004f);
					float inv = 1.0f / (d2 > 1 ? (float)__builtin_sqrtf(d2) : 1.0f);
					Engine::SetEntityVelocity(e, dx * inv * k, dy * inv * k, dz * inv * k);
					n++;
				}
		}
	}

	void PedCannon(uint8_t* ped);

	uint32_t s_cannonHandle = 0;
	int s_cannonTries = 0;
	float s_cannonDir[2] = {};

	void StepCannon()
	{
		if (!s_cannonTries) return;
		s_cannonTries--;
		for (uint32_t i = 0, cap = PoolCapacity(3); i < cap; i++) {
			uint8_t* e = PoolObject(3, i);
			if (!e || !Readable(e, 0xA0) || *(uint32_t*)(e + 0x9C) != s_cannonHandle) continue;
			Engine::SetEntityVelocity(e, s_cannonDir[0] * 45.0f, s_cannonDir[1] * 45.0f, 8.0f);
			if (s_cannonTries < 52) { Result("ped cannon fired"); s_cannonTries = 0; }   // keep pushing a few frames
			return;
		}
		if (!s_cannonTries) Result("cannon: clone not found");
	}

	// ---- named locations ----
	const int kMaxCats = 48;
	const char* s_catName[kMaxCats];
	uint16_t s_catFirst[kMaxCats], s_catCount[kMaxCats];
	uint16_t s_catItems[kLocationCount];
	int s_catTotal = 0;
	char s_locName[kLocationCount][36];
	int s_locNamesDone = 0;

	int CategoryOf(const char* name)
	{
		for (int c = 0; c < s_catTotal; c++) if (strcmp(s_catName[c], name) == 0) return c;
		if (s_catTotal >= kMaxCats) return -1;
		s_catName[s_catTotal] = name;
		return s_catTotal++;
	}

	void BuildLocationIndex()
	{
		uint16_t total[kMaxCats] = {};
		for (int i = 0; i < kLocationCount; i++) {
			if (kLocations[i].z < -50.0f) continue;
			int c = CategoryOf(kLocations[i].category);
			if (c >= 0) total[c]++;
		}
		int used = 0;
		for (int c = 0; c < s_catTotal; c++) { s_catFirst[c] = (uint16_t)used; used += total[c]; s_catCount[c] = 0; }
		for (int i = 0; i < kLocationCount; i++) {
			if (kLocations[i].z < -50.0f) continue;
			int c = CategoryOf(kLocations[i].category);
			if (c >= 0) s_catItems[s_catFirst[c] + s_catCount[c]++] = (uint16_t)i;
		}
	}

	void ResolveLocationNames()
	{
		for (int k = 0; k < 64 && s_locNamesDone < kLocationCount; k++, s_locNamesDone++) {
			int i = s_locNamesDone;
			if (!LabelText(kLocationLabel[i], s_locName[i], sizeof(s_locName[i])))
				snprintf(s_locName[i], sizeof(s_locName[i]), "%s", kLocations[i].name);
		}
	}

	struct Functor32;
	int CloneHandle(uint8_t* ped);

	struct Functor32 { void* cap; void* invoke; void* manager; void* destroy; };
	void ClonePlayer(uint8_t* ped)
	{
		static uint64_t dummy[2];
		Functor32 a = { dummy, At(kFunctorNop), At(kFunctorCopy), At(kFunctorNop) };
		Functor32 b = a;
		int h = Fn<int (*)(void*, int, int, int, int, int, Functor32, Functor32)>(kPedClone)(ped, 1, 0, 0, 0, 33, a, b);
		Result("clone: handle %08x", (unsigned)h);
	}

	int CloneHandle(uint8_t* ped)
	{
		static uint64_t dummy[2];
		Functor32 a = { dummy, At(kFunctorNop), At(kFunctorCopy), At(kFunctorNop) };
		return Fn<int (*)(void*, int, int, int, int, int, Functor32, Functor32)>(kPedClone)(ped, 1, 0, 0, 0, 33, a, a);
	}

	void PedCannon(uint8_t* ped)
	{
		int h = CloneHandle(ped);
		if (h == -1) { Result("cannon: clone failed"); return; }
		s_cannonHandle = (uint32_t)h;
		s_cannonTries = 60;
		s_cannonDir[0] = ((const float*)(ped + 0x50))[0];
		s_cannonDir[1] = ((const float*)(ped + 0x50))[1];
	}

	// ---- player data dump (for finding fields by diffing two dumps) ----
	int s_dumpNo = 0;
	int s_dumpFd = -1;

	void DumpChunk(const char* name, const void* p, uint32_t len)
	{
		if (s_dumpFd < 0 || !p || !Readable(p, len)) return;
		char hdr[32] = {};
		snprintf(hdr, 20, "%s", name);
		*(uint64_t*)(hdr + 20) = (uint64_t)p;
		*(uint32_t*)(hdr + 28) = len;
		sceKernelWrite(s_dumpFd, hdr, 32);
		sceKernelWrite(s_dumpFd, p, len);
	}

	// the block plus every readable heap pointer in its first `ptrs` qwords
	void DumpTree(const char* name, uint8_t* p, uint32_t len, int ptrs, uint32_t childLen)
	{
		if (!p || !Readable(p, len)) return;
		DumpChunk(name, p, len);
		for (int i = 0; i < ptrs && (uint32_t)(i * 8 + 8) <= len; i++) {
			uint8_t* c = *(uint8_t**)(p + i * 8);
			if ((uint64_t)c < 0x100000000ull || ((uint64_t)c & 7)) continue;
			char n[20];
			snprintf(n, sizeof(n), "%s+%x", name, i * 8);
			DumpChunk(n, c, childLen);
		}
	}

	void Dump(uint8_t* ped)
	{
		char path[64];
		snprintf(path, sizeof(path), "/data/BasicMenu/dump_%d.bin", s_dumpNo);
		s_dumpFd = sceKernelOpen(path, SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_TRUNC, 0666);
		if (s_dumpFd < 0) { Result("dump: cannot write %s", path); return; }
		uint32_t h = *(uint32_t*)(ped + 0x9C);
		DumpChunk("ped", ped, 0x800);
		DumpChunk("flags40", Fn<uint8_t* (*)(uint32_t)>(0x788D0)(h), 0x400);
		uint8_t* res = Fn<uint8_t* (*)(uint32_t)>(0x5F3320)(h);
		DumpTree("res15", res, 0x100, 32, 0x100);
		if (res) {
			uint8_t* r20 = *(uint8_t**)(res + 0x20);
			DumpTree("res15.20", r20, 0x200, 64, 0x200);
		}
		DumpChunk("player4A", Fn<uint8_t* (*)(uint32_t)>(0x1637050)(h), 0x6000);
		DumpChunk("stat", Fn<uint8_t* (*)(uint32_t)>(0x1421A0)(h), 0x11000);
		sceKernelClose(s_dumpFd);
		s_dumpFd = -1;
		Result("dump %d written", s_dumpNo);
		s_dumpNo++;
	}

	int s_vehDumpNo = 0;

	void DumpVehicle()
	{
		uint8_t* veh = *(uint8_t* volatile*)At(kLocalPlayerVeh);
		if (!veh) { Result("vehicle dump: no vehicle"); return; }
		char path[64];
		snprintf(path, sizeof(path), "/data/BasicMenu/veh_%d.bin", s_vehDumpNo);
		s_dumpFd = sceKernelOpen(path, SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_TRUNC, 0666);
		if (s_dumpFd < 0) { Result("vehicle dump: cannot write"); return; }
		uint32_t h = *(uint32_t*)(veh + 0x9C);
		DumpTree("veh", veh, 0x2000, 512, 0x800);       // object + everything it points to
		static const uint64_t kSafeGetters[] = { 0x747AA0, 0x69CB50, 0x1707F0, 0xDFB600 };
		int found = 0;
		for (uint64_t g : kSafeGetters) {
			uint8_t* c = Fn<uint8_t* (*)(uint32_t)>(g)(h);
			if (!c) continue;
			char n[20];
			snprintf(n, sizeof(n), "c%llx", (unsigned long long)g);
			DumpTree(n, c, 0x1000, 128, 0x400);
			found++;
		}
		sceKernelClose(s_dumpFd);
		s_dumpFd = -1;
		Result("vehicle dump %d: %d components", s_vehDumpNo, found);
		s_vehDumpNo++;
	}

	// every live object in the object pool (type 4), for diffing before / after the game creates one.
	// Written a few hundred objects per frame so the game thread never stalls on file I/O.
	int s_objDumpNo = 0, s_objFd = -1;
	uint32_t s_objPos = 0, s_objCount = 0;

	void StartObjectDump()
	{
		if (s_objFd >= 0) return;
		char path[64];
		snprintf(path, sizeof(path), "/data/BasicMenu/obj_%d.bin", s_objDumpNo);
		s_objFd = sceKernelOpen(path, SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_TRUNC, 0666);
		if (s_objFd < 0) { Result("object dump: cannot write"); return; }
		s_objPos = 0; s_objCount = 0;
		Result("object dump %d: writing...", s_objDumpNo);
	}

	void StepObjectDump()
	{
		if (s_objFd < 0) return;
		uint32_t cap = PoolCapacity(4);
		uint32_t end = s_objPos + 1500 < cap ? s_objPos + 1500 : cap;
		int written = 0;
		for (uint32_t i = s_objPos; i < end && written < 300; i++, s_objPos++) {
			uint8_t* e = PoolObject(4, i);
			if (!e || !Readable(e, 0x100)) continue;
			char hdr[32] = {};
			snprintf(hdr, 20, "o%u", i);
			*(uint64_t*)(hdr + 20) = (uint64_t)e;
			*(uint32_t*)(hdr + 28) = 0x100;
			sceKernelWrite(s_objFd, hdr, 32);
			sceKernelWrite(s_objFd, e, 0x100);
			written++; s_objCount++;
		}
		if (s_objPos >= cap) {
			sceKernelClose(s_objFd);
			s_objFd = -1;
			Result("object dump %d: %u objects", s_objDumpNo, s_objCount);
			s_objDumpNo++;
		}
	}

	// ---- item names: the item hash itself is often its text label ----
	const int kMaxItemNames = 1024;
	uint32_t s_itemHash[kMaxItemNames];
	char s_itemName[kMaxItemNames][40];
	int s_itemNames = 0;
	bool s_itemNamesDone = false;

	// Search an item's give-table row (and what its sub-entries point to) for a u32 that is a text label.
	int s_labelOffHits[64];
	bool FindLabelInRow(uint8_t* row, char* dst, size_t cap)
	{
		uint8_t* sub = *(uint8_t**)(row + 8);
		uint16_t cnt = *(uint16_t*)(row + 0x10);
		if (!sub || !cnt || !Readable(sub, 0x40)) return false;
		uint8_t* blocks[5] = { sub, nullptr, nullptr, nullptr, nullptr };
		int nb = 1;
		for (int q = 0; q < 8 && nb < 5; q++) {
			uint8_t* p = *(uint8_t**)(sub + 8 * q);
			if ((uint64_t)p > 0x100000000ull && !((uint64_t)p & 7) && Readable(p, 0x100)) blocks[nb++] = p;
		}
		for (int b = 0; b < nb; b++) {
			uint32_t len = b == 0 ? 0x40 : 0x100;
			for (uint32_t off = 0; off + 4 <= len; off += 4) {
				uint32_t v = *(uint32_t*)(blocks[b] + off);
				if (v < 0x10000 || v == 0xFFFFFFFF) continue;
				if (LabelText(v, dst, cap)) {
					int k = (b * 0x100 + off) / 4;
					if (k < 64) s_labelOffHits[k]++;
					return true;
				}
			}
		}
		return false;
	}

	void ResolveItemNames()
	{
		uint8_t* tbl = *(uint8_t* volatile*)At(0xC31EE78);
		uint32_t n = *(volatile uint16_t*)At(0xC31EE80);
		if (!tbl || !n || !Readable(tbl, 24ull * n)) return;
		if (n > kMaxItemNames) n = kMaxItemNames;
		int named = 0;
		int fd = sceKernelOpen("/data/BasicMenu/items.txt", SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_TRUNC, 0666);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t h = *(uint32_t*)(tbl + 24 * i);
			s_itemHash[i] = h;
			if (LabelText(h, s_itemName[i], sizeof(s_itemName[i]))) named++;
			else if (FindLabelInRow(tbl + 24 * i, s_itemName[i], sizeof(s_itemName[i]))) named++;
			if (fd >= 0) {
				char line[80];
				int len = snprintf(line, sizeof(line), "%3u %08x %s\n", i, h, s_itemName[i]);
				sceKernelWrite(fd, line, len);
			}
		}
		if (fd >= 0) sceKernelClose(fd);
		s_itemNames = (int)n;
		s_itemNamesDone = true;
		Log("item names: %d of %u resolved (list in items.txt)", named, n);
		for (int k = 0; k < 64; k++) if (s_labelOffHits[k]) Log("item label found at block %d +0x%x: %d items", k * 4 / 0x100, (k * 4) % 0x100, s_labelOffHits[k]);
	}

	// ---- money: cash stacks in the player's inventory (pure data; the readers 0x3BA94C0 / 0x3BA9500 sum them) ----
	int FindCashStacks(uint8_t* inv, uint8_t** out, int max)
	{
		uint8_t* v = *(uint8_t**)(inv + 0x10);
		if (!v || !Readable(v, 0x70)) return 0;
		uint32_t nb = *(uint32_t*)(v + 0x60);
		uint8_t* bk = *(uint8_t**)(v + 0x68);
		if (!bk || !nb || nb > 65536 || !Readable(bk, 400ull * nb)) return 0;
		int n = 0;
		for (uint32_t b = 0; b < nb && n < max; b++) {
			uint8_t* B = bk + 400 * b;
			for (int j = 0; j < 16 && n < max; j++) {
				if ((int8_t)B[j] < 0) continue;
				uint8_t* group = *(uint8_t**)(B + 16 + 24 * j + 0x10);
				if (!group || !Readable(group, 0x18)) continue;
				uint8_t** items = *(uint8_t***)(group + 8);
				uint16_t cnt = *(uint16_t*)(group + 0x10);
				if (!items || !Readable(items, 8ull * cnt)) continue;
				for (uint16_t k = 0; k < cnt && n < max; k++) {
					uint8_t* it = items[k];
					if (!it || !Readable(it, 0x48)) continue;
					uint8_t* def = *(uint8_t**)(it + 0x38);
					if (def && Readable(def, 0x10) && *(uint32_t*)(def + 8) == kCash) out[n++] = it;
				}
			}
		}
		return n;
	}

	void SetCash(uint8_t* ped, bool stash, int64_t amount, bool add)
	{
		uint8_t* inv = Inventory(ped);
		if (!inv) { Result("money: no inventory"); return; }
		int64_t before = stash ? Fn<int64_t (*)()>(0x3BA9500)() : Fn<int64_t (*)()>(0x3BA94C0)();
		uint8_t* stacks[32];
		int n = FindCashStacks(inv, stacks, 32);
		for (int i = 0; i < n; i++)
			Log("cash stack %d: %p amount %lld location %08x", i, stacks[i], (long long)*(int64_t*)(stacks[i] + 0x18),
				*(uint32_t*)(stacks[i] + 0x40));
		uint8_t* target = nullptr;
		for (int i = 0; i < n && !target; i++) if (*(int64_t*)(stacks[i] + 0x18) == before) target = stacks[i];
		if (!target) { Result(stash ? "money: stash stack not found" : "money: pick up some cash first"); return; }
		int64_t value = add ? before + amount : amount;
		if (value < 0) value = 0;
		uint8_t* v = *(uint8_t**)(inv + 0x10);
		Fn<void (*)(void*)>(kLock)(v + 0x20);
		*(int64_t*)(target + 0x18) = value;
		Fn<void (*)(void*)>(kUnlock)(v + 0x20);
		int64_t after = stash ? Fn<int64_t (*)()>(0x3BA9500)() : Fn<int64_t (*)()>(0x3BA94C0)();
		Result("%s $%lld -> $%lld", stash ? "stash" : "cash", (long long)before, (long long)after);
		ReadCash(ped);
	}

	// ---- request mailbox (menu thread -> game thread) ----
	std::atomic<int> s_reqWeather{-1}, s_reqOutfit{-1}, s_reqExtra{-1};
	std::atomic<bool> s_reqExtraOn{false}, s_reqOutfitList{false};
	std::atomic<int64_t> s_reqCash{0}, s_reqSetAmount{0};
	std::atomic<int> s_reqSetCash{-1};
	std::atomic<int> s_reqExplosion{-1};
	std::atomic<bool> s_reqClone{false}, s_reqDump{false}, s_reqVehDump{false};
	std::atomic<int> s_reqExplodeAll{-1}, s_reqDelete{0}, s_reqEngine{-1};
	std::atomic<bool> s_reqCopyLook{false}, s_reqCannon{false}, s_reqObjDump{false};
	std::atomic<int> s_reqDoor{-1}, s_reqInvis{-1};
	std::atomic<uint32_t> s_reqPaint{0};
}

void Init(uint64_t base)
{
	s_base = base;
	for (int i = 0; i < kWeatherNameCount; i++) s_weatherNameHash[i] = Joaat(kWeatherNames[i]);
	BuildLocationIndex();
}

void Tick(uint8_t* ped, uint64_t now)
{
	if (!s_vehLabelsDone && now - s_lastLabelTryMs > 5000) { s_lastLabelTryMs = now; ResolveVehicleLabels(); }
	LoadWeather();
	if (!s_itemNamesDone && s_vehLabelsDone) ResolveItemNames();
	if (now - s_lastCashMs > 1000) { s_lastCashMs = now; ReadCash(ped); }

	int w = s_reqWeather.exchange(-1);
	if (w >= 0 && w < s_weatherCount.load()) SetWeather(w);
	int64_t cash = s_reqCash.exchange(0);
	if (cash) AddCash(ped, cash);
	if (s_reqOutfitList.exchange(false)) ListOutfits();
	int sc = s_reqSetCash.exchange(-1);
	if (sc >= 0) SetCash(ped, sc & 1, s_reqSetAmount.load(), sc & 2);
	int o = s_reqOutfit.exchange(-1);
	if (o >= 0) StartOutfit(ped, o);
	StepOutfit(ped, now);
	int x = s_reqExtra.exchange(-1);
	if (x >= 0) SetExtra(x, s_reqExtraOn.load());
	int ex = s_reqExplosion.exchange(-1);
	if (ex >= 0) Explode(ped, ex);
	if (s_reqClone.exchange(false)) ClonePlayer(ped);
	if (s_vehLabelsDone && s_locNamesDone < kLocationCount) ResolveLocationNames();
	if (s_reqCopyLook.exchange(false)) CopyNearestLook(ped);
	int door = s_reqDoor.exchange(-1);
	if (door >= 0) Door(door & 0xFF, door >> 8);
	uint32_t paint = s_reqPaint.exchange(0);
	if (paint) Paint((paint >> 16) & 0xFF, (paint >> 8) & 0xFF, paint & 0xFF);
	int inv = s_reqInvis.exchange(-1);
	if (inv >= 0) Invisible(ped, inv & 1, inv & 2);
	if (s_reqCannon.exchange(false)) PedCannon(ped);
	if (s_reqObjDump.exchange(false)) StartObjectDump();
	StepObjectDump();
	StepFun(ped, now);
	StepCannon();
	int del = s_reqDelete.exchange(0);
	if (del) Result("%d deleted", DeleteNearby(ped, del & 1, del & 2));
	int eng = s_reqEngine.exchange(-1);
	if (eng >= 0) SetEngine(eng == 1);
	int ea = s_reqExplodeAll.exchange(-1);
	if (ea >= 0) Result("%d %s blown up", ExplodeAll(ped, ea == 1), ea == 1 ? "vehicles" : "peds");
	if (s_reqDump.exchange(false)) Dump(ped);
	if (s_reqVehDump.exchange(false)) DumpVehicle();
}

void RequestWeather(int index) { s_reqWeather.store(index); }
void RequestAddCash(int64_t amount) { s_reqCash.store(amount); }
void RequestOutfitList() { s_reqOutfitList.store(true); }
void RequestOutfit(int index) { s_reqOutfit.store(index); }
void RequestExplosion(int type) { s_reqExplosion.store(type); }
void RequestClone() { s_reqClone.store(true); }
void RequestCopyNearestLook() { s_reqCopyLook.store(true); }
void RequestDeleteNearby(bool vehicles, bool peds) { s_reqDelete.store((vehicles ? 1 : 0) | (peds ? 2 : 0)); }
void RequestEngine(bool on) { s_reqEngine.store(on ? 1 : 0); }
void RequestDoor(int part, bool open) { s_reqDoor.store((part & 0xFF) | (open ? 0x100 : 0)); }
void RequestPaint(uint8_t r, uint8_t g, uint8_t b) { s_reqPaint.store(0x1000000u | (r << 16) | (g << 8) | b); }
void RequestInvisible(bool vehicle, bool hide) { s_reqInvis.store((vehicle ? 1 : 0) | (hide ? 2 : 0)); }
void SetMeteorShower(bool on) { s_meteors = on; }
void SetBlackHole(bool on) { s_blackHole = on; }
void RequestPedCannon() { s_reqCannon.store(true); }
void RequestObjectDump() { s_reqObjDump.store(true); }

int LocationCategoryCount() { return s_catTotal; }
const char* LocationCategory(int c) { return c >= 0 && c < s_catTotal ? s_catName[c] : ""; }
int LocationCountIn(int c) { return c >= 0 && c < s_catTotal ? s_catCount[c] : 0; }
const char* LocationName(int c, int i)
{
	if (c < 0 || c >= s_catTotal || i < 0 || i >= s_catCount[c]) return "";
	int idx = s_catItems[s_catFirst[c] + i];
	return idx < s_locNamesDone ? s_locName[idx] : kLocations[idx].name;
}
void LocationPos(int c, int i, float* x, float* y, float* z)
{
	*x = *y = *z = 0;
	if (c < 0 || c >= s_catTotal || i < 0 || i >= s_catCount[c]) return;
	const NamedLocation& l = kLocations[s_catItems[s_catFirst[c] + i]];
	*x = l.x; *y = l.y; *z = l.z;
}

void RequestExplodeAll(bool vehicles) { s_reqExplodeAll.store(vehicles ? 1 : 0); }
void RequestDump() { s_reqDump.store(true); }
void RequestVehicleDump() { s_reqVehDump.store(true); }
void RequestExtra(int index, bool on) { s_reqExtraOn.store(on); s_reqExtra.store(index); }

const char* VehicleLabel(int i) { return i >= 0 && i < kVehicleModelCount ? s_vehLabel[i] : ""; }
int WeatherCount() { return s_weatherCount.load(); }
uint32_t WeatherHash(int i) { return i >= 0 && i < s_weatherCount.load() ? s_weather[i] : 0; }
const char* WeatherName(uint32_t h)
{
	for (int i = 0; i < kWeatherNameCount; i++) if (s_weatherNameHash[i] == h) return kWeatherNames[i];
	return nullptr;
}
int64_t Cash() { return s_cash.load(); }
int64_t StashCash() { return s_stash.load(); }
void RequestSetCash(bool stash, int64_t amount, bool add) { s_reqSetAmount.store(amount); s_reqSetCash.store((stash ? 1 : 0) | (add ? 2 : 0)); }
int OutfitCount() { return s_outfitCount.load(); }
uint32_t OutfitHash(int i) { return i >= 0 && i < s_outfitCount.load() ? s_outfits[i].outfit : 0; }
uint32_t OutfitVariant(int i) { return i >= 0 && i < s_outfitCount.load() ? s_outfits[i].variant : 0; }
const char* LastResult() { return s_result; }
const char* ItemLabel(uint32_t h)
{
	for (int i = 0; i < s_itemNames; i++) if (s_itemHash[i] == h) return s_itemName[i];
	return "";
}

}
