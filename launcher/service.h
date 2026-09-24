#pragma once

#include <string>

namespace launcher {

using NotifyFn = void (*)(const std::wstring& text);

// Background thread: auto-inject, periodic update checks, forwarding the mod's notifications.
void StartService(NotifyFn notify);
void StopService();

// Requests from the tray menu (handled on the service thread).
void RequestInjectNow();
void RequestUpdateCheck();
void RequestUnload();

// DLL that would be injected now (latest downloaded build, or BedrockQoL.dll next to the exe).
std::wstring CurrentDllPath();

// One-shot operations, also used by the command line (--inject, --check-update).
bool InjectCurrent(std::wstring& message);
bool CheckForUpdate(bool reloadIntoGame, std::wstring& message);

}  // namespace launcher
