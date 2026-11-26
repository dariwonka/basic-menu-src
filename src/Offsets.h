#pragma once
#include <stdint.h>

// PPSA38500 01.002.001. All values are relative to the image base.

namespace Offsets {

// import slots
const uint64_t GOT_scePadReadState             = 0xA3EA780;
const uint64_t GOT_sceVideoOutRegisterBuffers2 = 0xA3EAA28;
const uint64_t GOT_sceVideoOutSubmitFlip       = 0xA3EAA70;
const uint64_t GOT_sceAgcDcbSetFlip            = 0xA3EB6C0;

const uint64_t ImageSize = 0xE7C8000;

// cheat effect table, 16 function pointers
const uint64_t CheatTable      = 0x9ECE1D0;
const uint64_t CheatHealRepair = 0x53775C0;
const uint64_t CheatWantedUp   = 0x5377AC0;
const uint64_t CheatWantedDown = 0x5377D50;
const uint64_t CheatWeather    = 0x53780C0;

// functions
const uint64_t SpawnVehicle    = 0x5378380;   // (u64 modelHash)
const uint64_t ClockSetTime    = 0x537AFD0;   // (h, m, s)
const uint64_t GetPlayerComp   = 0x1637050;   // (u32 handle) -> comp*
const uint64_t SetPlayerWanted = 0x53ACD60;   // (ped*, level, 1, 1, 1)
const uint64_t HealthRestore   = 0x4F0C3B0;   // (u32 handle)
const uint64_t VehicleFix      = 0x80FEF80;   // (vehicle*)
const uint64_t StreamRequest   = 0x1908520;   // (u16* req, u32* model, ctx, 0, 9, 1, 0, aux)
const uint64_t StreamReadyA    = 0x693730;    // (u16* req) -> bool
const uint64_t StreamReadyB    = 0x685DF0;    // (u16* req) -> bool
const uint64_t StreamRelease   = 0x6B9DA0;    // (u16* req)

// data
const uint64_t LocalPlayerPed  = 0xDCDBCB0;   // ped*, handle at +0x9C
const uint64_t LocalPlayerVeh  = 0xDCEBCE0;   // vehicle*
const uint64_t GameClock       = 0xDCEC6D0;   // int[]: hour [4], minute [5]
const uint64_t StreamCtx       = 0xCC64B30;
const uint64_t ArchetypeMap    = 0xE561800;
const uint64_t LastCheatVeh    = 0xA753E4C;   // u32 handle

const uint32_t WantedLevel     = 0x5EFC;      // in the player component

}
