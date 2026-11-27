

#include "Notify.h"
#include <kernel.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace {
	struct NotifyRequest {
		int type;
		int reqId;
		int priority;
		int msgId;
		int targetId;
		int userId;
		int pad0[2];
		int appId;
		int errorNum;
		int pad1;
		char useIconUri;
		char message[1024];
		char iconUri[1024];
		char pad2[1024];
	};
	static_assert(sizeof(NotifyRequest) == 0xC30, "notify request size");
}

void Notify(const char* fmt, ...)
{
	NotifyRequest req;
	memset(&req, 0, sizeof(req));
	va_list args;
	va_start(args, fmt);
	vsnprintf(req.message, sizeof(req.message), fmt, args);
	va_end(args);
	req.targetId = -1;
	req.useIconUri = 1;
	strcpy(req.iconUri, "cxml://psnotification/tex_icon_system");

	int fd = sceKernelOpen("/dev/notification0", SCE_KERNEL_O_WRONLY, 0);
	if (fd < 0) fd = sceKernelOpen("/dev/notification1", SCE_KERNEL_O_WRONLY, 0);
	if (fd < 0) return;
	sceKernelWrite(fd, &req, sizeof(req));
	sceKernelClose(fd);
}
