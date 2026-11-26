#include <kernel.h>
#include <string.h>
#include "Log.h"
#include "Notify.h"
#include "Input.h"
#include "VideoOut.h"
#include "Overlay.h"
#include "Engine.h"
#include "Menu.h"
#include "Features.h"

namespace {
	struct Range { uint64_t start, end; };

	Range FindEboot()
	{
		Range r = { 0, 0 };
		void* addr = nullptr;
		SceKernelVirtualQueryInfo info;
		while (sceKernelVirtualQuery(addr, SCE_KERNEL_VQ_FIND_NEXT, &info, sizeof(info)) >= 0) {
			bool eboot = strstr(info.name, "eboot") ||
				(strstr(info.name, "executable") && (uint64_t)info.start < 0x100000000ull);
			if (eboot && info.isCommitted) {
				if (!r.start) r.start = (uint64_t)info.start;
				r.end = (uint64_t)info.end;
			}
			if ((uint64_t)info.end <= (uint64_t)info.start) break;
			addr = info.end;
		}
		return r;
	}

	void* MainThread(void*)
	{
		LogInit();
		Log("Basic Menu, built " __DATE__ " " __TIME__);

		Range eboot = { 0, 0 };
		for (int i = 0; i < 60 && !eboot.start; i++) {
			eboot = FindEboot();
			if (!eboot.start) sceKernelSleep(1);
		}
		if (!eboot.start) {
			Notify("Basic Menu: game not found");
			return nullptr;
		}
		Log("eboot %llx-%llx", (unsigned long long)eboot.start, (unsigned long long)eboot.end);

		VideoOut::Install(eboot.start, eboot.end);
		Engine::Init(eboot.start, eboot.end);
		if (!Input::Install(eboot.start, eboot.end)) {
			Notify("Basic Menu: unsupported game version");
			return nullptr;
		}

		Features::Init();
		Overlay::Start();
		Notify("Basic Menu loaded\nR3 + Right to open");

		uint64_t lastTick = 0;
		for (;;) {
			uint64_t now = sceKernelGetProcessTime() / 1000;
			if (now - lastTick >= 16) {
				lastTick = now;
				Menu::Tick();
			}
			Overlay::Poll();
			sceKernelUsleep(500);
		}
		return nullptr;
	}
}

extern "C" {

int module_start(size_t, const void*)
{
	static bool started = false;
	if (started) return 0;
	started = true;

	// keep the stack small, the game has little memory to spare
	ScePthreadAttr attr;
	scePthreadAttrInit(&attr);
	scePthreadAttrSetstacksize(&attr, 128 * 1024);
	ScePthread thread;
	scePthreadCreate(&thread, &attr, MainThread, nullptr, "BasicMenu");
	scePthreadAttrDestroy(&attr);
	scePthreadDetach(thread);
	return 0;
}

int module_stop(size_t, const void*)
{
	return 0;
}

}
