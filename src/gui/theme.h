#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "../config.h"
#include "imgui.h"

// Menu appearance: every ImGui color and size, a few colors of the mod's own widgets, the font and
// the background picture. Stored in config.ini ([Theme] + appearance keys of [Menu]), so it is part
// of every config profile, and exported/imported as JSON themes (BedrockQoL\themes\*.json).
namespace gui {

// Colors of the mod's own widgets, after the ImGuiCol_ ones.
enum CustomColor {
    kColAccent,         // titles, selected page, highlights
    kColSidebarBg,      // left navigation panel
    kColCardBg,         // module cards
    kColCardBgHovered,
    kColToggleOn,       // switch track when on
    kColToggleOff,      // switch track when off
    kColToggleKnob,
    kColScreenDim,      // darkens the game behind the menu
    kColToastBg,        // in-game notifications
    kCustomColorCount
};

enum class BackgroundMode { Fill, Fit, Stretch, Center, Tile };
enum class BackgroundTarget { Window, Screen };

struct Theme {
    ImGuiStyle style;  // unscaled sizes + ImGui colors
    ImVec4 custom[kCustomColorCount];

    float scale = 1.0f;     // whole menu, fonts included
    std::string font;       // "" = built-in Roboto
    float fontSize = 17.0f;

    std::string background;  // "" = none
    BackgroundMode backgroundMode = BackgroundMode::Fill;
    BackgroundTarget backgroundTarget = BackgroundTarget::Window;
    float backgroundOpacity = 0.35f;
    bool dimScreen = true;

    // Menu preferences kept next to the appearance in [Menu] (not part of JSON themes).
    float width = 860.0f;
    float height = 560.0f;
    bool notifications = true;
    bool animations = true;
};

// Built-in themes. The first one is the default.
struct Preset {
    const char* name;  // UTF-8, shown in the menu and accepted by ".theme preset <name>"
    Theme (*make)();
};
const std::vector<Preset>& Presets();
Theme DefaultTheme();

// Recolors the accent-dependent colors (checkmarks, sliders, selected tabs...).
void SetAccent(Theme& theme, const ImVec4& accent);

// --- Colors: ImGuiCol_* first, then CustomColor ---------------------------------------------
int ColorCount();
const char* ColorKey(int index);  // INI/JSON key ("WindowBg", "Accent")
std::string ColorLabel(int index);  // Russian name for the menu
ImVec4& ColorRef(Theme& theme, int index);
const ImVec4& ColorRef(const Theme& theme, int index);

std::string FormatColor(const ImVec4& color);  // #RRGGBBAA
bool ParseColor(const std::string& text, ImVec4& out);  // #RRGGBBAA, #RRGGBB, RRGGBBAA

// --- Sizes: fields of ImGuiStyle with 1 or 2 floats ------------------------------------------
struct SizeInfo {
    const char* key;    // INI/JSON key ("WindowRounding")
    const char* label;  // Russian name for the menu
    size_t offset;      // in ImGuiStyle
    int components;     // 1 = float, 2 = ImVec2
    float min;
    float max;
    const char* format;
};
const std::vector<SizeInfo>& Sizes();
float* SizeRef(ImGuiStyle& style, const SizeInfo& info);
std::string FormatSize(const ImGuiStyle& style, const SizeInfo& info);

// --- Persistence ---------------------------------------------------------------------------
using Section = std::vector<std::pair<std::string, std::string>>;

// Builds the theme from config.ini ([Theme] and [Menu] sections, as read by config::ReadSection).
Theme ThemeFromIni(const Section& themeSection, const Section& menuSection);

// Changes that make config.ini hold `theme` (only values that differ from the default theme are
// stored). With `appearanceOnly`, the menu preferences (size, notifications, animations) are kept.
std::vector<ConfigEntry> ThemeToIni(const Theme& theme, bool appearanceOnly);

// Single-value helpers used by the editor ("Set", or "Remove" when the value is the default).
ConfigEntry ColorEntry(const Theme& theme, int index);
ConfigEntry SizeEntry(const Theme& theme, const SizeInfo& info);
ConfigEntry MenuEntry(const std::string& key, const std::string& value);  // [Menu] keys are always written

const char* BackgroundModeName(BackgroundMode mode);
const char* BackgroundTargetName(BackgroundTarget target);

// JSON themes (nlohmann/json). ThemeFromJson starts from the default theme, so a partial file works.
std::string ThemeToJson(const Theme& theme, const std::string& name);
bool ThemeFromJson(const std::string& json, Theme& out, std::string& error);

// Style with Scale applied, ready for ImGui::GetStyle().
ImGuiStyle ScaledStyle(const Theme& theme);

}  // namespace gui
