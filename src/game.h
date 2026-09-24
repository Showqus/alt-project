#pragma once

#include <string>

namespace game {

// "Microsoft.MinecraftUWP_1.21.5101.0_x64__8wekyb3d8bbwe" or empty if not packaged.
std::wstring PackageFullName();

// Folder the DLL can write its config/log into (created if needed).
std::wstring DataDirectory();

// True when the mouse cursor is hidden, i.e. the player is in the world and not in a menu/chat.
// Honors Config::requireHiddenCursor (always true when that option is off).
bool InWorld();

// Cheaper variant for per-frame use: re-checks the cursor at most every ~50 ms.
bool InWorldCached();

// True when the game window has keyboard focus (used only by the polling fallback).
bool HasFocus();

}  // namespace game
