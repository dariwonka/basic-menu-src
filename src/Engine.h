#pragma once
#include <stdint.h>

// Game calls for PPSA38500 01.002.001. Menu code only queues work; GameTick (on the game's own thread,
// from the pad hook) runs it. A version check on the cheat effect table turns everything off on any
// other game version, and the options lock while PSN is reachable (story mode only).

namespace Engine {

enum class Op { HealRepair, WantedUp, WantedDown, NextWeather, SetHour, SpawnVehicle, SetWanted, SpawnModel,
	GiveItem, GiveCash, GiveItemUnlocked, GiveCashUnlocked, TeleportUp, TeleportForward, ServiceCheck, SaveLocation, GotoLocation,
	VehicleBoost, VehicleJump };

bool Init(uint64_t ebootBase, uint64_t ebootEnd);   // version gate; false = features off
bool Ready();                                      // gate passed and not locked
const char* LockReason();                          // why not Ready (for the menu)

void Queue(Op op, int64_t arg = 0);                // from the menu thread
void GameTick();                               // from the pad hook (game thread)

// Toggles applied every game-thread tick.
void SetNeverWanted(bool on);
void SetFreezeTime(bool on);
void SetGodMode(bool on);
void SetInfiniteArmour(bool on);
void SetSpawnInside(bool on);
void SetSeat(int seat);
void SetTimeScale(int percent);        // 1..100, 100 = normal
void SetEmptyCity(bool on);

// test toggles (meaning not confirmed yet)
enum class Test { InfiniteAmmo, Stamina, EntB0Bit21, EntB0Bit7, Ent34Bit20, Count };
void SetTest(Test t, bool on);
void SetSuperJump(bool on);
void SetVehicleGod(bool on);
void SetInfiniteClip(bool on);
void SetInfiniteAmmo(bool on);
void SetWantedLock(int level);              // -1 off
void SetEntityVelocity(uint8_t* ent, float vx, float vy, float vz);   // game thread only
void TeleportTo(float x, float y, float z);

// groups of candidate config flags, switched together
enum class FlagGroup { Proofs, NoRagdoll, SeatBelt, InfiniteClip, PoliceIgnore, Stamina, DamageBits, Count };
void SetFlagGroup(FlagGroup g, bool on);
void SetConfigFlag(int id, bool on);   // ped config flag 0..1115 on the player
bool GetConfigFlag(int id);

// saved locations (kept in /data/BasicMenu/locations.txt)
int LocationCount();
void LocationText(int index, char* out, int cap);

// item table, read from the game once the player exists
int ItemCount();
uint32_t ItemHash(int index);
const char* ItemName(uint32_t hash);   // nullptr if unknown
uint32_t LastGiveResult();

// Live values for the Status page (updated on the game thread).
struct Status {
	uint64_t ped;
	uint32_t handle;
	int wanted;          // -1 if unknown
	int hour, minute;
};
Status GetStatus();

const int kVehicleCount = 8;

}
