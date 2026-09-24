#pragma once

#include <string>

namespace game {

// "Microsoft.MinecraftUWP_1.21.5101.0_x64__8wekyb3d8bbwe" or empty if not packaged.
std::wstring PackageFullName();

// Folder the DLL can write its config/log into (created if needed).
std::wstring DataDirectory();

// True when there is evidence that the game has captured the mouse, i.e. the player is in the
// world: Windows reports the cursor hidden (or no cursor at all), or the game reports only relative
// mouse movement while the pointer position stays put.
bool CursorHidden();

// CursorHidden was true at least once this session. Until then the cursor cannot tell a game screen
// from the world (some systems always report a visible cursor), so it does not block anything.
bool CursorDetectionWorks();

// Input thread (MouseDevice::feed): a pointer move, for the relative-movement check above.
void OnMouseMove(int x, int y, int dx, int dy);

// Input thread: the chat was opened, the pointer is free again (even if it has not moved yet).
void OnScreenKey();

// True when the player is in the world and not in a menu or the chat: neither the chat nor the mod's
// menu is open and, if Config::requireHiddenCursor is on, the cursor is hidden (or the cursor
// detection does not work on this system).
bool InWorld();

// The cursor alone does not rule out the world (InWorld without the chat / menu checks).
bool CursorAllowsWorld();

// Cheaper variant for per-frame use: re-checks the cursor at most every ~50 ms.
bool InWorldCached();

// True when the game window has keyboard focus (used only by the polling fallback).
bool HasFocus();

// Seconds since the game process started.
double ProcessAgeSeconds();

}  // namespace game
