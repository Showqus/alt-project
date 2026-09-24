#pragma once

#include "imgui.h"
#include "theme.h"

// Small custom widgets of the menu. Render thread only.
namespace gui::widgets {

// Pill-shaped on/off switch. `id` should be "##something".
bool Toggle(const char* id, bool* value, const Theme& theme);

// Button that shows a key name ("F8", "Нет") or a prompt while waiting for a key.
bool KeyButton(const char* id, int vk, bool waiting, float width);

// "(?)" marker with a tooltip.
void Help(const char* text);

// Text in the accent color / larger size.
void Heading(const char* text, float sizeFactor, const ImVec4& color);

// Label on the left (with an optional "(?)" tooltip), widget on the right side of the available
// width (call before the widget).
void RowLabel(const char* label, float widgetWidth, const char* help = nullptr);

// Dim text wrapped to the available width.
void Hint(const char* text);

// Solid color swatch button used for theme presets.
bool Swatch(const char* id, const ImVec4& color, const ImVec2& size);

}  // namespace gui::widgets
