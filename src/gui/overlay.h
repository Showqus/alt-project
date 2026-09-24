#pragma once

#include <windows.h>

#include <string>

// The in-game menu is drawn by hooking IDXGISwapChain::Present: right before the game presents a
// frame, ImGui draws into the same back buffer with the game's own Direct3D 11 or 12 device.
namespace gui::overlay {

// Worker thread, after MH_Initialize: finds IDXGISwapChain::Present / ResizeBuffers (and
// ID3D12CommandQueue::ExecuteCommandLists when the game uses Direct3D 12) through throw-away
// devices and hooks them. Returns false if the menu will not be available.
bool Install();

// Worker thread, before the hooks are removed: destroys the menu on the render thread (or here, if
// the game stopped presenting) so no GPU object or ImGui state outlives the DLL.
void Shutdown();

// The menu can be shown: a renderer is set up and the game presented a frame recently.
bool Ready();

// Short description for the log / menu: "Direct3D 11, 1920x1080" or why the menu is unavailable.
std::string Status();

// Back buffer size and the game window (render/input threads).
void DisplaySize(float& width, float& height);
HWND GameWindow();

}  // namespace gui::overlay
