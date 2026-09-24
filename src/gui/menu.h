#pragma once

struct ImGuiIO;

// The in-game menu: modules, key binds, appearance, configs, general settings, and the in-game
// notifications. Render thread only (called by the overlay around each ImGui frame).
namespace gui::menu {

// After ImGui::CreateContext: fonts.
void Init();

// True if a frame has to be drawn: the menu is open or fading, or notifications are showing.
bool WantsFrame();

// Before ImGui::NewFrame: applies the theme, font and background, replays the game's input.
void BeginFrame(ImGuiIO& io);

// Between ImGui::NewFrame and ImGui::Render.
void Draw();

// Before ImGui::DestroyContext (after the renderer released its GPU objects).
void Shutdown();

}  // namespace gui::menu
