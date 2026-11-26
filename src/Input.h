#pragma once
#include <stdint.h>

namespace Input {
	bool Install(uint64_t base, uint64_t end);

	void Poll();                       // once per menu tick
	uint32_t Held();
	uint32_t Pressed();                // went down since the last Poll
	bool Repeat(uint32_t button);      // pressed, then auto-repeat while held

	void Block(uint32_t mask);         // hide these buttons from the game (0 = none)
}
