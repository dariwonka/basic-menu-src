#include "Log.h"
#include <kernel.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_DIR  "/data/BasicMenu"
#define LOG_FILE LOG_DIR "/menu.log"

static ScePthreadMutex s_mutex;
static bool s_ready = false;

void LogInit()
{
	sceKernelMkdir(LOG_DIR, 0777);
	scePthreadMutexInit(&s_mutex, nullptr, "MenuLog");
	s_ready = true;
}

void Log(const char* fmt, ...)
{
	char line[1024];
	SceKernelTimespec ts;
	sceKernelClockGettime(SCE_KERNEL_CLOCK_MONOTONIC, &ts);
	int n = snprintf(line, sizeof(line), "[%6lld.%03ld] ", (long long)ts.tv_sec, ts.tv_nsec / 1000000);

	va_list args;
	va_start(args, fmt);
	vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
	va_end(args);
	strcat(line, "\n");

	printf("%s", line);
	if (!s_ready) return;

	scePthreadMutexLock(&s_mutex);
	int fd = sceKernelOpen(LOG_FILE, SCE_KERNEL_O_WRONLY | SCE_KERNEL_O_CREAT | SCE_KERNEL_O_APPEND, 0666);
	if (fd >= 0) {
		sceKernelWrite(fd, line, strlen(line));
		sceKernelClose(fd);
	}
	scePthreadMutexUnlock(&s_mutex);
}
