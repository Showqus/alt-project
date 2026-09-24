#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace logx {
namespace {

CRITICAL_SECTION g_lock;
bool g_lockReady = false;
FILE* g_file = nullptr;

void Write(const char* level, const char* fmt, va_list args) {
    char message[1024];
    vsnprintf(message, sizeof(message), fmt, args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1200];
    snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] [%s] %s\n", t.wHour, t.wMinute, t.wSecond,
             t.wMilliseconds, level, message);

    OutputDebugStringA(line);

    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (g_file) {
        fputs(line, g_file);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_lock);
}

}  // namespace

void Init(const std::wstring& path) {
    if (!g_lockReady) {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }
    EnterCriticalSection(&g_lock);
    if (!g_file) g_file = _wfopen(path.c_str(), L"w");
    LeaveCriticalSection(&g_lock);
}

void Shutdown() {
    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
    LeaveCriticalSection(&g_lock);
}

void Info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write("INFO", fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write("WARN", fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write("ERROR", fmt, args);
    va_end(args);
}

}  // namespace logx
