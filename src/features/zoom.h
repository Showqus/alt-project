#pragma once

namespace zoom {

// Zoom key state changed (called from the input thread / polling thread).
void OnKey(bool down, bool inWorld);

// Mouse wheel while zooming. Returns true if the event should be swallowed.
bool OnScroll(int delta, bool inWorld);

// Called from the LevelRendererPlayer::getFov hook with the game's FOV; returns the FOV to use.
float OnFov(float fov);

// Drops the zoom immediately (unload / feature disabled).
void Reset();

}  // namespace zoom
