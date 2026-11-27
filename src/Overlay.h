

#pragma once
#include <stdint.h>

// Draws the menu with the CPU straight into the game's display buffer, right after each flip.

namespace Overlay {
	void Start();
	void Poll();                      // call every ~0.5 ms
	void Publish(bool open);          // called by Menu::Tick
	void SetVideoHandle(int32_t handle);
}
