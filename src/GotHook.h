#pragma once
#include <stdint.h>

// Swaps one import slot of the eboot for `hook`. The current target is stored in *orig
// before the slot changes, so the hook can call through it right away.
// Returns false if the slot doesn't hold a bound import (wrong game version).
bool HookImport(const char* name, uint64_t slotOffset, void* hook, uint64_t base, uint64_t end, void* volatile* orig);
