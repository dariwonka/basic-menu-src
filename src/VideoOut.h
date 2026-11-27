

#pragma once
#include <stdint.h>

namespace VideoOut {
	bool Install(uint64_t base, uint64_t end);

	int BufferCount();
	uint64_t BufferAddress(int index);
}
