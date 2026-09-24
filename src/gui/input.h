#pragma once

#include <string>
#include <vector>

struct ImGuiIO;

// Game input -> menu. The game's Keyboard::feed / MouseDevice::feed hooks run on the game's input
// thread; ImGui runs on the render thread, so events are queued here and replayed into ImGui
// before each menu frame.
namespace gui::input {

// --- Input thread -------------------------------------------------------------------------

// Every Keyboard::feed event. Opens the menu with the menu key; while the menu is open every key
// is queued for it. Returns true if the event must not reach the game.
bool OnKey(int vk, bool down, bool pressed);

// Every MouseDevice::feed event (button 0 = move, 1/2/3 = left/right/middle, 4 = wheel).
// Returns true if the event must not reach the game (the menu is open).
bool OnMouse(int button, int action, int x, int y, int dx, int dy);

// --- Render thread ------------------------------------------------------------------------

struct KeyEvent {
    int vk = 0;
    bool down = false;
    std::wstring chars;  // characters typed by this key press (current layout)
};

// Moves queued mouse events into `io` and returns the queued key events; the caller decides which
// of them reach ImGui (see FeedKey). Polls the keyboard / mouse instead when the game hooks are
// unavailable.
void Drain(ImGuiIO& io, std::vector<KeyEvent>& keys);

// Sends one key event (and its characters) to ImGui.
void FeedKey(ImGuiIO& io, const KeyEvent& key);

// Forget held keys/buttons (the menu was reopened).
void Reset(ImGuiIO& io);

}  // namespace gui::input
