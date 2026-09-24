#pragma once

#include <windows.h>

#include <string>

namespace launcher {

DWORD FindProcess(const std::wstring& exeName);

// Seconds since the process was created (0 if unknown).
double ProcessAgeSeconds(DWORD pid);

// Name of the first loaded module whose name starts with "BedrockQoL" (empty if none).
std::wstring LoadedModDll(DWORD pid);

// Gives "ALL APPLICATION PACKAGES" read+execute access, required for UWP apps to load the file.
bool GrantAppContainerAccess(const std::wstring& path);

// LoadLibraryW in the target process. `error` receives a human readable reason on failure.
bool InjectDll(DWORD pid, const std::wstring& dllPath, std::wstring& error);

}  // namespace launcher
