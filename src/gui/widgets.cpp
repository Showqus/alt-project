#include "widgets.h"

#include <string>

#include "../keys.h"
#include "imgui_internal.h"

namespace gui::widgets {

bool Toggle(const char* id, bool* value, const Theme& theme) {
    const float height = ImGui::GetFrameHeight() * 0.85f;
    const float width = height * 1.9f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float offsetY = (ImGui::GetFrameHeight() - height) * 0.5f;

    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetFrameHeight()))) {
        *value = !*value;
        changed = true;
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    // Knob slides between the ends (animated by ImGui's per-item timer while the value just changed).
    ImGuiContext& g = *ImGui::GetCurrentContext();
    float t = *value ? 1.0f : 0.0f;
    if (g.LastActiveId == g.LastItemData.ID && g.LastActiveIdTimer < 0.12f) {
        const float progress = g.LastActiveIdTimer / 0.12f;
        t = *value ? progress : 1.0f - progress;
    }

    const ImVec4 off = theme.custom[kColToggleOff];
    const ImVec4 on = theme.custom[kColToggleOn];
    ImVec4 track(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t,
                 off.w + (on.w - off.w) * t);
    if (hovered) track = ImVec4(track.x * 1.1f, track.y * 1.1f, track.z * 1.1f, track.w);
    const ImVec2 a(pos.x, pos.y + offsetY);
    const ImVec2 b(pos.x + width, pos.y + offsetY + height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(a, b, ImGui::GetColorU32(track), height * 0.5f);
    const float radius = height * 0.5f - 2.0f;
    const ImVec2 knob(a.x + radius + 2.0f + t * (width - 2.0f * radius - 4.0f), a.y + height * 0.5f);
    draw->AddCircleFilled(knob, radius, ImGui::GetColorU32(theme.custom[kColToggleKnob]));
    return changed;
}

bool KeyButton(const char* id, int vk, bool waiting, float width) {
    std::string label = waiting ? "Нажмите клавишу..." : (vk > 0 ? keys::Name(vk) : "Нет");
    label += id;
    if (waiting) {
        const ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImGui::PushStyleColor(ImGuiCol_Button, active);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active);
    }
    const bool clicked = ImGui::Button(label.c_str(), ImVec2(width, 0.0f));
    if (waiting) ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !waiting) {
        ImGui::SetTooltip("Нажмите, затем нужную клавишу.\nEsc - отмена, Backspace/Delete - убрать клавишу.");
    }
    return clicked;
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Heading(const char* text, float sizeFactor, const ImVec4& color) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * sizeFactor);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void RowLabel(const char* label, float widgetWidth, const char* help) {
    const float start = ImGui::GetCursorPosX();
    const float right = start + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) Help(help);
    ImGui::SameLine();
    if (right - widgetWidth > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right - widgetWidth);
    ImGui::SetNextItemWidth(widgetWidth);
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool Swatch(const char* id, const ImVec4& color, const ImVec2& size) {
    return ImGui::ColorButton(id, color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, size);
}

}  // namespace gui::widgets
