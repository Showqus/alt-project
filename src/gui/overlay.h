#pragma once

#include <windows.h>

#include <string>

// The in-game menu is drawn by hooking IDXGISwapChain::Present: right before the game presents a
// frame, ImGui draws into the same back buffer with the game's own Direct3D 11 or 12 device.
namespace gui::overlay {

// Worker thread, after MH_Initialize: finds IDXGISwapChain::Present / ResizeBuffers (and
// ID3D12CommandQueue::ExecuteCommandLists when the game uses Direct3D 12) through throw-away
// devices and hooks them. Returns false if the menu will not be available: switched off
// ([Menu] Enabled=0), or the previous game session died while the menu was starting (then it
// switches itself off and tells the player). Does nothing once it succeeded.
bool Start();

// Runs Start on its own thread (Direct3D test devices may take long or hang in a driver; key binds
// and commands on the worker thread must not wait for them). Does nothing while it runs.
void StartAsync();

// Worker thread, periodically: tells the player if the set-up has been stuck for 10 seconds.
void Watch();

// The set-up thread is running.
bool Starting();

// Start was called (it is running, or it succeeded).
bool Started();

// The game is exiting normally (DllMain, DLL_PROCESS_DETACH): not a crash.
void OnProcessExit();

// Worker thread, before the hooks are removed: destroys the menu on the render thread (or here, if
// the game stopped presenting) so no GPU object or ImGui state outlives the DLL. Returns false if the
// set-up thread is stuck inside a driver: then the DLL must stay loaded.
bool Shutdown();

// The menu can be shown: a renderer is set up and the game presented a frame recently.
bool Ready();

// Short description for the log / menu: "Direct3D 11, 1920x1080" or why the menu is unavailable.
std::string Status();

// Back buffer size and the game window (render/input threads).
void DisplaySize(float& width, float& height);
HWND GameWindow();

}  // namespace gui::overlay
