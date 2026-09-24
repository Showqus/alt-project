#pragma once

#include <string>

namespace logx {

// Opens (truncates) the log file. Safe to call once at startup.
void Init(const std::wstring& path);
void Shutdown();

// printf-style logging, one line per call.
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);

}  // namespace logx
