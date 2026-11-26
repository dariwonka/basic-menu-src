#include "VideoOut.h"
#include "GotHook.h"
#include "Offsets.h"
#include "Overlay.h"
#include "Log.h"
#include <video_out.h>
#include <atomic>

namespace {
	typedef int32_t (*RegisterBuffers2_t)(int32_t, int32_t, int32_t, const SceVideoOutBuffers*, int32_t,
		const SceVideoOutBufferAttribute2*, int32_t, void*);
	typedef int32_t (*SubmitFlip_t)(int32_t, int32_t, uint32_t, int64_t);
	typedef uint64_t (*DcbSetFlip_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

	RegisterBuffers2_t volatile s_register = nullptr;
	SubmitFlip_t volatile s_submitFlip = nullptr;
	DcbSetFlip_t volatile s_dcbSetFlip = nullptr;

	const int kMaxBuffers = 16;
	uint64_t s_buffers[kMaxBuffers];
	std::atomic<int> s_count{0};

	int32_t RegisterBuffers2Hook(int32_t handle, int32_t set, int32_t start, const SceVideoOutBuffers* buffers,
		int32_t num, const SceVideoOutBufferAttribute2* attr, int32_t category, void* option)
	{
		int32_t ret = s_register(handle, set, start, buffers, num, attr, category, option);
		Overlay::SetVideoHandle(handle);
		if (attr)
			Log("display %ux%u fmt 0x%llx tiling %d", attr->width, attr->height,
				(unsigned long long)attr->pixelFormat, attr->tilingMode);
		for (int i = 0; buffers && i < num; i++) {
			int slot = start + i;
			if (slot < 0 || slot >= kMaxBuffers) continue;
			s_buffers[slot] = (uint64_t)buffers[i].data;
			if (slot + 1 > s_count.load()) s_count.store(slot + 1);
		}
		return ret;
	}

	int32_t SubmitFlipHook(int32_t handle, int32_t index, uint32_t mode, int64_t arg)
	{
		Overlay::SetVideoHandle(handle);
		return s_submitFlip(handle, index, mode, arg);
	}

	uint64_t DcbSetFlipHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f)
	{
		Overlay::SetVideoHandle((int32_t)b);
		return s_dcbSetFlip(a, b, c, d, e, f);
	}
}

namespace VideoOut {

bool Install(uint64_t base, uint64_t end)
{
	bool a = HookImport("sceVideoOutRegisterBuffers2", Offsets::GOT_sceVideoOutRegisterBuffers2,
		(void*)RegisterBuffers2Hook, base, end, (void* volatile*)&s_register);
	bool b = HookImport("sceVideoOutSubmitFlip", Offsets::GOT_sceVideoOutSubmitFlip,
		(void*)SubmitFlipHook, base, end, (void* volatile*)&s_submitFlip);
	bool c = HookImport("sceAgcDcbSetFlip", Offsets::GOT_sceAgcDcbSetFlip,
		(void*)DcbSetFlipHook, base, end, (void* volatile*)&s_dcbSetFlip);
	return a || b || c;
}

int BufferCount() { return s_count.load(); }

uint64_t BufferAddress(int index)
{
	return index >= 0 && index < s_count.load() ? s_buffers[index] : 0;
}

}
