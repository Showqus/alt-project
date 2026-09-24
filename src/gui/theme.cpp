#include "theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../text.h"
#include "json.hpp"

namespace gui {
namespace {

using json = nlohmann::json;

ImVec4 Hex(unsigned rgba) {
    return ImVec4(static_cast<float>((rgba >> 24) & 0xFF) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f);
}

ImVec4 WithAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 Lighter(const ImVec4& c, float t) { return WithAlpha(Mix(c, ImVec4(1, 1, 1, 1), t), c.w); }

const char* const kCustomKeys[kCustomColorCount] = {
    "Accent", "SidebarBg", "CardBg", "CardBgHovered", "ToggleOn", "ToggleOff", "ToggleKnob", "ScreenDim", "ToastBg",
};

const char* const kCustomLabels[kCustomColorCount] = {
    "Акцент",
    "Боковая панель",
    "Карточка модуля",
    "Карточка модуля (наведение)",
    "Переключатель: включён",
    "Переключатель: выключен",
    "Переключатель: ручка",
    "Затемнение игры за меню",
    "Фон уведомлений",
};

struct ColorName {
    const char* key;
    const char* label;
};

// Russian names of the ImGui colors; "...Hovered" / "...Active" variants are derived from the base name.
const ColorName kColorLabels[] = {
    {"Text", "Текст"},
    {"TextDisabled", "Текст (неактивный)"},
    {"WindowBg", "Фон окна"},
    {"ChildBg", "Фон панелей"},
    {"PopupBg", "Фон всплывающих окон"},
    {"Border", "Рамка"},
    {"BorderShadow", "Тень рамки"},
    {"FrameBg", "Фон полей"},
    {"TitleBg", "Заголовок окна"},
    {"TitleBgCollapsed", "Заголовок окна (свёрнуто)"},
    {"MenuBarBg", "Строка меню"},
    {"ScrollbarBg", "Фон прокрутки"},
    {"ScrollbarGrab", "Ползунок прокрутки"},
    {"CheckMark", "Галочка"},
    {"CheckboxSelectedBg", "Фон отмеченного флажка"},
    {"SliderGrab", "Ползунок слайдера"},
    {"Button", "Кнопка"},
    {"Header", "Строка списка"},
    {"Separator", "Разделитель"},
    {"ResizeGrip", "Уголок размера окна"},
    {"InputTextCursor", "Курсор ввода"},
    {"Tab", "Вкладка"},
    {"TabSelected", "Вкладка (выбрана)"},
    {"TabSelectedOverline", "Линия выбранной вкладки"},
    {"TabDimmed", "Вкладка (окно неактивно)"},
    {"TabDimmedSelected", "Выбранная вкладка (окно неактивно)"},
    {"TabDimmedSelectedOverline", "Линия вкладки (окно неактивно)"},
    {"PlotLines", "Линии графика"},
    {"PlotHistogram", "Гистограмма"},
    {"TableHeaderBg", "Заголовок таблицы"},
    {"TableBorderStrong", "Внешняя граница таблицы"},
    {"TableBorderLight", "Линии таблицы"},
    {"TableRowBg", "Строка таблицы"},
    {"TableRowBgAlt", "Строка таблицы (чередование)"},
    {"TextLink", "Ссылка"},
    {"TextSelectedBg", "Выделенный текст"},
    {"TreeLines", "Линии дерева"},
    {"DragDropTarget", "Цель перетаскивания"},
    {"DragDropTargetBg", "Фон цели перетаскивания"},
    {"UnsavedMarker", "Маркер несохранённого"},
    {"NavCursor", "Рамка навигации с клавиатуры"},
    {"NavWindowingHighlight", "Подсветка окна (Ctrl+Tab)"},
    {"NavWindowingDimBg", "Затемнение (Ctrl+Tab)"},
    {"ModalWindowDimBg", "Затемнение (модальные окна)"},
};

const char* FindLabel(const std::string& key) {
    for (const ColorName& c : kColorLabels) {
        if (key == c.key) return c.label;
    }
    return nullptr;
}

// --- Built-in themes ---------------------------------------------------------------------

void DefaultSizes(ImGuiStyle& s) {
    s.Alpha = 1.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.WindowRounding = 10.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildRounding = 8.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding = 8.0f;
    s.PopupBorderSize = 1.0f;
    s.FramePadding = ImVec2(10, 6);
    s.FrameRounding = 6.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.IndentSpacing = 20.0f;
    s.CellPadding = ImVec2(8, 6);
    s.ScrollbarSize = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabMinSize = 10.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.TabBorderSize = 0.0f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
}

Theme MakeDark(unsigned accent) {
    Theme t;
    ImGuiStyle& s = t.style;
    ImGui::StyleColorsDark(&s);
    DefaultSizes(s);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Hex(0xE8EAF0FF);
    c[ImGuiCol_TextDisabled] = Hex(0x8A90A0FF);
    c[ImGuiCol_WindowBg] = Hex(0x111318F5);
    c[ImGuiCol_ChildBg] = Hex(0x00000000);
    c[ImGuiCol_PopupBg] = Hex(0x16181FFA);
    c[ImGuiCol_Border] = Hex(0x2A2E3AFF);
    c[ImGuiCol_BorderShadow] = Hex(0x00000000);
    c[ImGuiCol_FrameBg] = Hex(0x1D2029FF);
    c[ImGuiCol_FrameBgHovered] = Hex(0x262A36FF);
    c[ImGuiCol_FrameBgActive] = Hex(0x2D3240FF);
    c[ImGuiCol_TitleBg] = Hex(0x0E1015FF);
    c[ImGuiCol_TitleBgActive] = Hex(0x14161DFF);
    c[ImGuiCol_TitleBgCollapsed] = Hex(0x0E101580);
    c[ImGuiCol_MenuBarBg] = Hex(0x14161DFF);
    c[ImGuiCol_ScrollbarBg] = Hex(0x00000000);
    c[ImGuiCol_ScrollbarGrab] = Hex(0x2A2E3AFF);
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x363B4AFF);
    c[ImGuiCol_CheckboxSelectedBg] = Hex(0x2D3240FF);
    c[ImGuiCol_Button] = Hex(0x232733FF);
    c[ImGuiCol_ButtonHovered] = Hex(0x2E3342FF);
    c[ImGuiCol_Header] = Hex(0x232733FF);
    c[ImGuiCol_HeaderHovered] = Hex(0x2E3342FF);
    c[ImGuiCol_Separator] = Hex(0x2A2E3AFF);
    c[ImGuiCol_SeparatorHovered] = Hex(0x3A4050FF);
    c[ImGuiCol_ResizeGrip] = Hex(0x2A2E3A60);
    c[ImGuiCol_ResizeGripHovered] = Hex(0x3A405099);
    c[ImGuiCol_InputTextCursor] = Hex(0xE8EAF0FF);
    c[ImGuiCol_TabHovered] = Hex(0x2E3342FF);
    c[ImGuiCol_Tab] = Hex(0x1A1D25FF);
    c[ImGuiCol_TabSelected] = Hex(0x262A36FF);
    c[ImGuiCol_TabDimmed] = Hex(0x15171DFF);
    c[ImGuiCol_TabDimmedSelected] = Hex(0x1D2029FF);
    c[ImGuiCol_TabDimmedSelectedOverline] = Hex(0x00000000);
    c[ImGuiCol_PlotLines] = Hex(0x9AA0B0FF);
    c[ImGuiCol_TableHeaderBg] = Hex(0x1A1D25FF);
    c[ImGuiCol_TableBorderStrong] = Hex(0x2A2E3AFF);
    c[ImGuiCol_TableBorderLight] = Hex(0x22252FFF);
    c[ImGuiCol_TableRowBg] = Hex(0x00000000);
    c[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF06);
    c[ImGuiCol_TreeLines] = Hex(0x2A2E3AFF);
    c[ImGuiCol_DragDropTargetBg] = Hex(0x00000000);
    c[ImGuiCol_UnsavedMarker] = Hex(0xE8EAF0FF);
    c[ImGuiCol_ModalWindowDimBg] = Hex(0x00000080);

    t.custom[kColSidebarBg] = Hex(0x0B0C10E6);
    t.custom[kColCardBg] = Hex(0x181B23FF);
    t.custom[kColCardBgHovered] = Hex(0x1D212BFF);
    t.custom[kColToggleOff] = Hex(0x3A3F4DFF);
    t.custom[kColToggleKnob] = Hex(0xFFFFFFFF);
    t.custom[kColScreenDim] = Hex(0x00000066);
    t.custom[kColToastBg] = Hex(0x16181FF0);
    SetAccent(t, Hex(accent));
    return t;
}

Theme MakeLight() {
    Theme t;
    ImGuiStyle& s = t.style;
    ImGui::StyleColorsLight(&s);
    DefaultSizes(s);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Hex(0x1E2230FF);
    c[ImGuiCol_TextDisabled] = Hex(0x7A8090FF);
    c[ImGuiCol_WindowBg] = Hex(0xF4F5F8F5);
    c[ImGuiCol_ChildBg] = Hex(0x00000000);
    c[ImGuiCol_PopupBg] = Hex(0xFFFFFFFA);
    c[ImGuiCol_Border] = Hex(0xD5D8E0FF);
    c[ImGuiCol_BorderShadow] = Hex(0x00000000);
    c[ImGuiCol_FrameBg] = Hex(0xE6E8EEFF);
    c[ImGuiCol_FrameBgHovered] = Hex(0xDDE0E8FF);
    c[ImGuiCol_FrameBgActive] = Hex(0xD2D6E0FF);
    c[ImGuiCol_TitleBg] = Hex(0xE6E8EEFF);
    c[ImGuiCol_TitleBgActive] = Hex(0xDDE0E8FF);
    c[ImGuiCol_ScrollbarBg] = Hex(0x00000000);
    c[ImGuiCol_ScrollbarGrab] = Hex(0xC8CCD6FF);
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0xB5BAC6FF);
    c[ImGuiCol_CheckboxSelectedBg] = Hex(0xD2D6E0FF);
    c[ImGuiCol_Button] = Hex(0xE3E6ECFF);
    c[ImGuiCol_ButtonHovered] = Hex(0xD6DAE3FF);
    c[ImGuiCol_Header] = Hex(0xE3E6ECFF);
    c[ImGuiCol_HeaderHovered] = Hex(0xD6DAE3FF);
    c[ImGuiCol_Separator] = Hex(0xD5D8E0FF);
    c[ImGuiCol_InputTextCursor] = Hex(0x1E2230FF);
    c[ImGuiCol_Tab] = Hex(0xE6E8EEFF);
    c[ImGuiCol_TabHovered] = Hex(0xD6DAE3FF);
    c[ImGuiCol_TabSelected] = Hex(0xFFFFFFFF);
    c[ImGuiCol_TableHeaderBg] = Hex(0xE6E8EEFF);
    c[ImGuiCol_TableBorderStrong] = Hex(0xD5D8E0FF);
    c[ImGuiCol_TableBorderLight] = Hex(0xE3E6ECFF);
    c[ImGuiCol_TableRowBgAlt] = Hex(0x0000000A);
    c[ImGuiCol_ModalWindowDimBg] = Hex(0x00000040);

    t.custom[kColSidebarBg] = Hex(0xE9EBF0F0);
    t.custom[kColCardBg] = Hex(0xFFFFFFFF);
    t.custom[kColCardBgHovered] = Hex(0xF7F8FBFF);
    t.custom[kColToggleOff] = Hex(0xC3C8D2FF);
    t.custom[kColToggleKnob] = Hex(0xFFFFFFFF);
    t.custom[kColScreenDim] = Hex(0x00000040);
    t.custom[kColToastBg] = Hex(0xFFFFFFF0);
    SetAccent(t, Hex(0x3B82F6FF));
    return t;
}

// Blocky stone-and-grass look.
Theme MakeMinecraft() {
    Theme t = MakeDark(0x5BA02EFF);
    ImGuiStyle& s = t.style;
    s.WindowRounding = s.ChildRounding = s.PopupRounding = s.FrameRounding = 0.0f;
    s.ScrollbarRounding = s.GrabRounding = s.TabRounding = 0.0f;
    s.WindowBorderSize = 2.0f;
    s.FrameBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = Hex(0xF0F0F0FF);
    c[ImGuiCol_TextDisabled] = Hex(0xA0A0A0FF);
    c[ImGuiCol_WindowBg] = Hex(0x1E1E1EF2);
    c[ImGuiCol_PopupBg] = Hex(0x232323FA);
    c[ImGuiCol_Border] = Hex(0x0A0A0AFF);
    c[ImGuiCol_FrameBg] = Hex(0x3A3A3AFF);
    c[ImGuiCol_FrameBgHovered] = Hex(0x464646FF);
    c[ImGuiCol_FrameBgActive] = Hex(0x505050FF);
    c[ImGuiCol_Button] = Hex(0x4A4A4AFF);
    c[ImGuiCol_ButtonHovered] = Hex(0x5A5A5AFF);
    c[ImGuiCol_Header] = Hex(0x3A3A3AFF);
    c[ImGuiCol_HeaderHovered] = Hex(0x4A4A4AFF);
    c[ImGuiCol_Tab] = Hex(0x2E2E2EFF);
    c[ImGuiCol_TabHovered] = Hex(0x4A4A4AFF);
    c[ImGuiCol_TabSelected] = Hex(0x3A3A3AFF);
    c[ImGuiCol_Separator] = Hex(0x0A0A0AFF);
    t.custom[kColSidebarBg] = Hex(0x141414EB);
    t.custom[kColCardBg] = Hex(0x2B2B2BFF);
    t.custom[kColCardBgHovered] = Hex(0x333333FF);
    t.custom[kColToggleOff] = Hex(0x555555FF);
    t.custom[kColToggleKnob] = Hex(0xE0E0E0FF);
    return t;
}

Theme MakeViolet() { return MakeDark(0x8B5CF6FF); }
Theme MakeBlue() { return MakeDark(0x3B82F6FF); }
Theme MakeEmerald() { return MakeDark(0x10B981FF); }
Theme MakeCrimson() { return MakeDark(0xEF4444FF); }
Theme MakeAmber() { return MakeDark(0xF59E0BFF); }

// --- Parsing helpers -----------------------------------------------------------------------

std::string FormatFloat(float v) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f", v);
    std::string s = buffer;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

bool ParseFloat(const std::string& text, float& out) {
    const std::string t = text::Trim(text, " \t");
    if (t.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (end == t.c_str() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool ParseBool(const std::string& v, bool def) {
    if (v.empty()) return def;
    const char c = v[0];
    return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

bool EqualsNoCase(const std::string& a, const char* b) {
    return text::Lower(a) == text::Lower(std::string(b));
}

int FindColor(const std::string& key) {
    for (int i = 0; i < ColorCount(); ++i) {
        if (EqualsNoCase(key, ColorKey(i))) return i;
    }
    return -1;
}

const SizeInfo* FindSize(const std::string& key) {
    for (const SizeInfo& info : Sizes()) {
        if (EqualsNoCase(key, info.key)) return &info;
    }
    return nullptr;
}

bool SetSize(ImGuiStyle& style, const SizeInfo& info, const float* values, int count) {
    if (count < 1) return false;
    float* p = SizeRef(style, info);
    for (int i = 0; i < info.components; ++i) {
        const float v = values[count > i ? i : 0];
        p[i] = std::clamp(v, info.min, info.max);
    }
    return true;
}

bool ParseSize(const std::string& text, ImGuiStyle& style, const SizeInfo& info) {
    float values[2] = {};
    int count = 0;
    size_t pos = 0;
    while (count < 2 && pos <= text.size()) {
        const size_t comma = text.find(',', pos);
        const std::string part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!ParseFloat(part, values[count])) return false;
        ++count;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return SetSize(style, info, values, count);
}

BackgroundMode ParseMode(const std::string& v, BackgroundMode def) {
    const std::string l = text::Lower(text::Trim(v, " \t"));
    for (BackgroundMode m : {BackgroundMode::Fill, BackgroundMode::Fit, BackgroundMode::Stretch, BackgroundMode::Center,
                             BackgroundMode::Tile}) {
        if (l == BackgroundModeName(m)) return m;
    }
    return def;
}

BackgroundTarget ParseTarget(const std::string& v, BackgroundTarget def) {
    const std::string l = text::Lower(text::Trim(v, " \t"));
    if (l == "window") return BackgroundTarget::Window;
    if (l == "screen") return BackgroundTarget::Screen;
    return def;
}

void ApplyMenuKey(Theme& t, const std::string& key, const std::string& value) {
    float f = 0.0f;
    if (EqualsNoCase(key, "Font")) {
        t.font = value;
    } else if (EqualsNoCase(key, "FontSize")) {
        if (ParseFloat(value, f)) t.fontSize = std::clamp(f, 8.0f, 64.0f);
    } else if (EqualsNoCase(key, "Scale")) {
        if (ParseFloat(value, f)) t.scale = std::clamp(f, 0.5f, 3.0f);
    } else if (EqualsNoCase(key, "Width")) {
        if (ParseFloat(value, f)) t.width = std::clamp(f, 400.0f, 4000.0f);
    } else if (EqualsNoCase(key, "Height")) {
        if (ParseFloat(value, f)) t.height = std::clamp(f, 300.0f, 3000.0f);
    } else if (EqualsNoCase(key, "Background")) {
        t.background = value;
    } else if (EqualsNoCase(key, "BackgroundMode")) {
        t.backgroundMode = ParseMode(value, t.backgroundMode);
    } else if (EqualsNoCase(key, "BackgroundTarget")) {
        t.backgroundTarget = ParseTarget(value, t.backgroundTarget);
    } else if (EqualsNoCase(key, "BackgroundOpacity")) {
        if (ParseFloat(value, f)) t.backgroundOpacity = std::clamp(f, 0.0f, 1.0f);
    } else if (EqualsNoCase(key, "DimScreen")) {
        t.dimScreen = ParseBool(value, t.dimScreen);
    } else if (EqualsNoCase(key, "Notifications")) {
        t.notifications = ParseBool(value, t.notifications);
    } else if (EqualsNoCase(key, "Animations")) {
        t.animations = ParseBool(value, t.animations);
    }
}

ConfigEntry Set(const char* section, const std::string& key, const std::string& value) {
    ConfigEntry e;
    e.section = section;
    e.key = key;
    e.value = value;
    return e;
}

}  // namespace

// --- Presets -------------------------------------------------------------------------------

const std::vector<Preset>& Presets() {
    static const std::vector<Preset> presets = {
        {"Фиолетовая", &MakeViolet}, {"Синяя", &MakeBlue},   {"Изумрудная", &MakeEmerald},
        {"Алая", &MakeCrimson},      {"Янтарная", &MakeAmber}, {"Светлая", &MakeLight},
        {"Minecraft", &MakeMinecraft},
    };
    return presets;
}

Theme DefaultTheme() { return MakeViolet(); }

void SetAccent(Theme& t, const ImVec4& accent) {
    ImVec4* c = t.style.Colors;
    const ImVec4 a = WithAlpha(accent, 1.0f);
    const ImVec4 light = Lighter(a, 0.25f);
    c[ImGuiCol_CheckMark] = a;
    c[ImGuiCol_SliderGrab] = a;
    c[ImGuiCol_SliderGrabActive] = light;
    c[ImGuiCol_ScrollbarGrabActive] = a;
    c[ImGuiCol_ButtonActive] = WithAlpha(a, 0.85f);
    c[ImGuiCol_HeaderActive] = WithAlpha(a, 0.80f);
    c[ImGuiCol_SeparatorActive] = a;
    c[ImGuiCol_ResizeGripActive] = a;
    c[ImGuiCol_TabSelectedOverline] = a;
    c[ImGuiCol_PlotLinesHovered] = a;
    c[ImGuiCol_PlotHistogram] = a;
    c[ImGuiCol_PlotHistogramHovered] = light;
    c[ImGuiCol_TextLink] = light;
    c[ImGuiCol_TextSelectedBg] = WithAlpha(a, 0.35f);
    c[ImGuiCol_DragDropTarget] = a;
    c[ImGuiCol_NavCursor] = a;
    t.custom[kColAccent] = a;
    t.custom[kColToggleOn] = a;
}

// --- Colors --------------------------------------------------------------------------------

int ColorCount() { return ImGuiCol_COUNT + kCustomColorCount; }

const char* ColorKey(int index) {
    if (index < ImGuiCol_COUNT) return ImGui::GetStyleColorName(index);
    return kCustomKeys[index - ImGuiCol_COUNT];
}

std::string ColorLabel(int index) {
    if (index >= ImGuiCol_COUNT) return kCustomLabels[index - ImGuiCol_COUNT];
    const std::string key = ColorKey(index);
    if (const char* label = FindLabel(key)) return label;
    for (const char* suffix : {"Hovered", "Active"}) {
        const size_t n = strlen(suffix);
        if (key.size() > n && key.compare(key.size() - n, n, suffix) == 0) {
            if (const char* base = FindLabel(key.substr(0, key.size() - n))) {
                return std::string(base) + (suffix[0] == 'H' ? " (наведение)" : " (нажатие)");
            }
        }
    }
    return key;
}

ImVec4& ColorRef(Theme& theme, int index) {
    return index < ImGuiCol_COUNT ? theme.style.Colors[index] : theme.custom[index - ImGuiCol_COUNT];
}

const ImVec4& ColorRef(const Theme& theme, int index) {
    return index < ImGuiCol_COUNT ? theme.style.Colors[index] : theme.custom[index - ImGuiCol_COUNT];
}

std::string FormatColor(const ImVec4& c) {
    auto byte = [](float v) { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", byte(c.x), byte(c.y), byte(c.z), byte(c.w));
    return buffer;
}

bool ParseColor(const std::string& text, ImVec4& out) {
    std::string t = text::Trim(text, " \t");
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() != 6 && t.size() != 8) return false;
    for (char ch : t) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    }
    unsigned value = static_cast<unsigned>(std::strtoul(t.c_str(), nullptr, 16));
    if (t.size() == 6) value = (value << 8) | 0xFF;
    out = Hex(value);
    return true;
}

// --- Sizes ---------------------------------------------------------------------------------

#define BQOL_SIZE(field, label, n, lo, hi, fmt) {#field, label, offsetof(ImGuiStyle, field), n, lo, hi, fmt}

const std::vector<SizeInfo>& Sizes() {
    static const std::vector<SizeInfo> sizes = {
        BQOL_SIZE(Alpha, "Непрозрачность меню", 1, 0.2f, 1.0f, "%.2f"),
        BQOL_SIZE(WindowPadding, "Отступы окна", 2, 0.0f, 40.0f, "%.0f"),
        BQOL_SIZE(WindowRounding, "Скругление окна", 1, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(WindowBorderSize, "Рамка окна", 1, 0.0f, 4.0f, "%.0f"),
        BQOL_SIZE(ChildRounding, "Скругление карточек и панелей", 1, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(ChildBorderSize, "Рамка карточек и панелей", 1, 0.0f, 4.0f, "%.0f"),
        BQOL_SIZE(PopupRounding, "Скругление всплывающих окон", 1, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(PopupBorderSize, "Рамка всплывающих окон", 1, 0.0f, 4.0f, "%.0f"),
        BQOL_SIZE(FramePadding, "Отступы в кнопках и полях", 2, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(FrameRounding, "Скругление кнопок и полей", 1, 0.0f, 20.0f, "%.0f"),
        BQOL_SIZE(FrameBorderSize, "Рамка кнопок и полей", 1, 0.0f, 4.0f, "%.0f"),
        BQOL_SIZE(ItemSpacing, "Промежутки между элементами", 2, 0.0f, 40.0f, "%.0f"),
        BQOL_SIZE(ItemInnerSpacing, "Промежутки внутри элементов", 2, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(CellPadding, "Отступы в таблицах", 2, 0.0f, 30.0f, "%.0f"),
        BQOL_SIZE(IndentSpacing, "Отступ вложенных пунктов", 1, 0.0f, 60.0f, "%.0f"),
        BQOL_SIZE(ScrollbarSize, "Толщина полосы прокрутки", 1, 2.0f, 30.0f, "%.0f"),
        BQOL_SIZE(ScrollbarRounding, "Скругление полосы прокрутки", 1, 0.0f, 20.0f, "%.0f"),
        BQOL_SIZE(GrabMinSize, "Размер ползунков", 1, 4.0f, 40.0f, "%.0f"),
        BQOL_SIZE(GrabRounding, "Скругление ползунков", 1, 0.0f, 20.0f, "%.0f"),
        BQOL_SIZE(TabRounding, "Скругление вкладок", 1, 0.0f, 20.0f, "%.0f"),
        BQOL_SIZE(TabBorderSize, "Рамка вкладок", 1, 0.0f, 4.0f, "%.0f"),
    };
    return sizes;
}

#undef BQOL_SIZE

float* SizeRef(ImGuiStyle& style, const SizeInfo& info) {
    return reinterpret_cast<float*>(reinterpret_cast<char*>(&style) + info.offset);
}

std::string FormatSize(const ImGuiStyle& style, const SizeInfo& info) {
    const float* p = reinterpret_cast<const float*>(reinterpret_cast<const char*>(&style) + info.offset);
    return info.components == 1 ? FormatFloat(p[0]) : FormatFloat(p[0]) + "," + FormatFloat(p[1]);
}

// --- config.ini ----------------------------------------------------------------------------

const char* BackgroundModeName(BackgroundMode mode) {
    switch (mode) {
        case BackgroundMode::Fit: return "fit";
        case BackgroundMode::Stretch: return "stretch";
        case BackgroundMode::Center: return "center";
        case BackgroundMode::Tile: return "tile";
        case BackgroundMode::Fill: break;
    }
    return "fill";
}

const char* BackgroundTargetName(BackgroundTarget target) {
    return target == BackgroundTarget::Screen ? "screen" : "window";
}

Theme ThemeFromIni(const Section& themeSection, const Section& menuSection) {
    Theme t = DefaultTheme();
    for (const auto& [key, value] : themeSection) {
        const int color = FindColor(key);
        if (color >= 0) {
            ParseColor(value, ColorRef(t, color));
        } else if (const SizeInfo* size = FindSize(key)) {
            ParseSize(value, t.style, *size);
        }
    }
    for (const auto& [key, value] : menuSection) ApplyMenuKey(t, key, value);
    return t;
}

ConfigEntry ColorEntry(const Theme& theme, int index) {
    static const Theme defaults = DefaultTheme();
    ConfigEntry e = Set("Theme", ColorKey(index), FormatColor(ColorRef(theme, index)));
    if (e.value == FormatColor(ColorRef(defaults, index))) e.op = ConfigEntry::Op::Remove;
    return e;
}

ConfigEntry SizeEntry(const Theme& theme, const SizeInfo& info) {
    static const Theme defaults = DefaultTheme();
    ConfigEntry e = Set("Theme", info.key, FormatSize(theme.style, info));
    if (e.value == FormatSize(defaults.style, info)) e.op = ConfigEntry::Op::Remove;
    return e;
}

ConfigEntry MenuEntry(const std::string& key, const std::string& value) { return Set("Menu", key, value); }

std::vector<ConfigEntry> ThemeToIni(const Theme& theme, bool appearanceOnly) {
    std::vector<ConfigEntry> entries;
    ConfigEntry clear;
    clear.op = ConfigEntry::Op::RemoveSection;
    clear.section = "Theme";
    entries.push_back(clear);
    for (int i = 0; i < ColorCount(); ++i) {
        ConfigEntry e = ColorEntry(theme, i);
        if (e.op == ConfigEntry::Op::Set) entries.push_back(e);
    }
    for (const SizeInfo& info : Sizes()) {
        ConfigEntry e = SizeEntry(theme, info);
        if (e.op == ConfigEntry::Op::Set) entries.push_back(e);
    }
    entries.push_back(MenuEntry("Scale", FormatFloat(theme.scale)));
    entries.push_back(MenuEntry("Font", theme.font));
    entries.push_back(MenuEntry("FontSize", FormatFloat(theme.fontSize)));
    entries.push_back(MenuEntry("Background", theme.background));
    entries.push_back(MenuEntry("BackgroundMode", BackgroundModeName(theme.backgroundMode)));
    entries.push_back(MenuEntry("BackgroundTarget", BackgroundTargetName(theme.backgroundTarget)));
    entries.push_back(MenuEntry("BackgroundOpacity", FormatFloat(theme.backgroundOpacity)));
    entries.push_back(MenuEntry("DimScreen", theme.dimScreen ? "1" : "0"));
    if (!appearanceOnly) {
        entries.push_back(MenuEntry("Width", FormatFloat(theme.width)));
        entries.push_back(MenuEntry("Height", FormatFloat(theme.height)));
        entries.push_back(MenuEntry("Notifications", theme.notifications ? "1" : "0"));
        entries.push_back(MenuEntry("Animations", theme.animations ? "1" : "0"));
    }
    return entries;
}

// --- JSON ----------------------------------------------------------------------------------

std::string ThemeToJson(const Theme& theme, const std::string& name) {
    json colors = json::object();
    for (int i = 0; i < ColorCount(); ++i) colors[ColorKey(i)] = FormatColor(ColorRef(theme, i));

    json sizes = json::object();
    for (const SizeInfo& info : Sizes()) {
        const float* p = reinterpret_cast<const float*>(reinterpret_cast<const char*>(&theme.style) + info.offset);
        if (info.components == 1) {
            sizes[info.key] = std::stod(FormatFloat(p[0]));
        } else {
            sizes[info.key] = json::array({std::stod(FormatFloat(p[0])), std::stod(FormatFloat(p[1]))});
        }
    }

    json root = {
        {"format", "BedrockQoL theme"},
        {"version", 1},
        {"name", name},
        {"scale", std::stod(FormatFloat(theme.scale))},
        {"font", {{"file", theme.font}, {"size", std::stod(FormatFloat(theme.fontSize))}}},
        {"background",
         {{"image", theme.background},
          {"mode", BackgroundModeName(theme.backgroundMode)},
          {"target", BackgroundTargetName(theme.backgroundTarget)},
          {"opacity", std::stod(FormatFloat(theme.backgroundOpacity))},
          {"dimScreen", theme.dimScreen}}},
        {"colors", colors},
        {"sizes", sizes},
    };
    return root.dump(2);
}

bool ThemeFromJson(const std::string& text, Theme& out, std::string& error) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "файл не является JSON-объектом";
        return false;
    }

    Theme t = DefaultTheme();
    t.width = out.width;
    t.height = out.height;
    t.notifications = out.notifications;
    t.animations = out.animations;

    if (auto it = root.find("colors"); it != root.end() && it->is_object()) {
        for (auto& [key, value] : it->items()) {
            const int index = FindColor(key);
            if (index >= 0 && value.is_string()) ParseColor(value.get<std::string>(), ColorRef(t, index));
        }
    }
    if (auto it = root.find("sizes"); it != root.end() && it->is_object()) {
        for (auto& [key, value] : it->items()) {
            const SizeInfo* info = FindSize(key);
            if (!info) continue;
            float values[2] = {};
            int count = 0;
            if (value.is_number()) {
                values[count++] = value.get<float>();
            } else if (value.is_array()) {
                for (const json& v : value) {
                    if (count < 2 && v.is_number()) values[count++] = v.get<float>();
                }
            }
            SetSize(t.style, *info, values, count);
        }
    }
    if (auto it = root.find("scale"); it != root.end() && it->is_number()) {
        t.scale = std::clamp(it->get<float>(), 0.5f, 3.0f);
    }
    if (auto it = root.find("font"); it != root.end() && it->is_object()) {
        if (auto f = it->find("file"); f != it->end() && f->is_string()) t.font = f->get<std::string>();
        if (auto f = it->find("size"); f != it->end() && f->is_number()) {
            t.fontSize = std::clamp(f->get<float>(), 8.0f, 64.0f);
        }
    }
    if (auto it = root.find("background"); it != root.end() && it->is_object()) {
        if (auto b = it->find("image"); b != it->end() && b->is_string()) t.background = b->get<std::string>();
        if (auto b = it->find("mode"); b != it->end() && b->is_string()) {
            t.backgroundMode = ParseMode(b->get<std::string>(), t.backgroundMode);
        }
        if (auto b = it->find("target"); b != it->end() && b->is_string()) {
            t.backgroundTarget = ParseTarget(b->get<std::string>(), t.backgroundTarget);
        }
        if (auto b = it->find("opacity"); b != it->end() && b->is_number()) {
            t.backgroundOpacity = std::clamp(b->get<float>(), 0.0f, 1.0f);
        }
        if (auto b = it->find("dimScreen"); b != it->end() && b->is_boolean()) t.dimScreen = b->get<bool>();
    }
    out = t;
    return true;
}

// --- ImGui ---------------------------------------------------------------------------------

ImGuiStyle ScaledStyle(const Theme& theme) {
    ImGuiStyle s = theme.style;
    s.ScaleAllSizes(theme.scale);
    s.FontSizeBase = theme.fontSize;
    s.FontScaleMain = theme.scale;
    s.FontScaleDpi = 1.0f;
    return s;
}

}  // namespace gui
