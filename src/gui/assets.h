#pragma once

#include <string>
#include <vector>

#include "imgui.h"

// Files the menu uses: fonts, background pictures and JSON themes. Fonts and textures are
// render-thread only; the folder helpers work on any thread.
namespace gui::assets {

// BedrockQoL\<name> (created if missing): "fonts", "images", "themes", "configs".
std::wstring Folder(const wchar_t* name);

// Names of the files in a folder with one of the extensions (".ttf;.otf"), sorted.
std::vector<std::string> List(const wchar_t* folder, const char* extensions);

// Font files in C:\Windows\Fonts (cached).
const std::vector<std::string>& SystemFonts();

// --- Render thread -----------------------------------------------------------------------

// Adds the built-in font (first font of the atlas). Call once after ImGui::CreateContext.
void Init();

// Loads `name` (empty = built-in) if it is not the current font. Call before ImGui::NewFrame.
void SetFont(const std::string& name);
const std::string& FontError();  // why the last font could not be loaded ("" = OK)

// Loads the background picture `name` (empty = none) if it changed. Call before ImGui::NewFrame.
void SetBackground(const std::string& name);
const std::string& BackgroundError();
// Current background, or false if there is none.
bool Background(ImTextureRef& texture, ImVec2& size);

// Frees textures that the renderer released (every frame) / everything (before DestroyContext).
void Update();
void Shutdown();

}  // namespace gui::assets
