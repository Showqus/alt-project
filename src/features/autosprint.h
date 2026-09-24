#pragma once

namespace autosprint {

// Signature of the game's Keyboard::feed(key, state).
using KeyFeedFn = void (*)(int key, int state);

// Hook mode: called for every key event inside the Keyboard::feed hook, before the event is
// passed on. May feed extra sprint-key events through `feed`. Returns true to swallow the event.
bool OnKeyEvent(int vk, bool down, KeyFeedFn feed);

// Hook mode: releases the injected sprint key (must be called on the input thread).
void Release(KeyFeedFn feed);

// Fallback mode (no keyboard hook): called periodically from the worker thread.
void PollFallback(bool focused, bool inWorld);

// Fallback mode: releases a sprint key that was pressed with SendInput.
void ReleaseFallback();

void Toggle();

}  // namespace autosprint
