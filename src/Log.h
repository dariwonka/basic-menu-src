

#pragma once

void LogInit();
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
