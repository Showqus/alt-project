#pragma once

#include <windows.h>

namespace hooks {

// Scans for the game functions and installs every hook that could be found.
// Returns false only if MinHook itself failed to initialise.
bool Install();

// Disables and removes all hooks.
void Uninstall();

bool HasFovHook();
bool HasKeyboardHook();
bool HasMouseHook();

// Number of Keyboard::feed calls seen so far (used to detect a hook that never fires).
unsigned KeyboardEventCount();

// When set, the keyboard hook stops handling keys and the worker thread polls instead.
void SetKeyboardFallback(bool enabled);
bool KeyboardFallback();

// Signalled when the player presses the unload key.
HANDLE UnloadEvent();

}  // namespace hooks
