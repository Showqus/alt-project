#include "menu.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../config.h"
#include "../game.h"
#include "../hooks.h"
#include "../keys.h"
#include "../text.h"
#include "assets.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "input.h"
#include "overlay.h"
#include "state.h"
#include "theme.h"
#include "widgets.h"

#ifndef BEDROCKQOL_VERSION
#define BEDROCKQOL_VERSION "dev"
#endif

namespace gui::menu {
namespace {

enum class Page { Modules, Binds, Appearance, Configs, General };

struct PageInfo {
    Page page;
    const char* label;
    const char* title;
    const char* subtitle;
};

const PageInfo kPages[] = {
    {Page::Modules, "   Модули##nav.modules", "Модули", "Включайте функции и настраивайте их"},
    {Page::Binds, "   Бинды##nav.binds", "Бинды", "Клавиши всех функций в одном месте"},
    {Page::Appearance, "   Внешний вид##nav.appearance", "Внешний вид", "Цвета, размеры, шрифт и фон меню"},
    {Page::Configs, "   Конфиги##nav.configs", "Конфиги", "Профили со всеми настройками, включая внешний вид"},
    {Page::General, "   Общие##nav.general", "Общие", "Поведение мода и информация"},
};

// --- State (render thread) -----------------------------------------------------------------

Page g_page = Page::Modules;
Theme g_theme = DefaultTheme();
std::shared_ptr<const Snapshot> g_snapshot;
ULONGLONG g_waitingSince = 0;  // snapshot held back because our own writes are still in flight

std::vector<ConfigEntry> g_themeEdits;  // appearance edits, written once the user stops editing
ULONGLONG g_lastThemeEdit = 0;

bool g_wasOpen = false;
float g_fade = 0.0f;
bool g_applyWindowSize = true;
ImVec2 g_lastWindowSize(0, 0);
bool g_userResizing = false;
float g_scaleEdit = -1.0f;  // scale slider value while dragging (applied on release)

struct Capture {
    bool active = false;
    std::string id;  // widget id of the key button
    std::string section;
    std::string key;
    std::atomic<int> Config::*field = nullptr;
    std::string textHotkeyId;  // TextHotkeys entry instead of a [section] key
    std::string textHotkeyText;
    int frame = 0;
};
Capture g_capture;
bool g_captureButtonHovered = false;

std::vector<Toast> g_toasts;
std::set<std::string> g_expanded;
std::map<std::string, bool> g_cardHovered;

struct TextBuffer {
    std::array<char, 256> text{};
    uint64_t version = 0;
};
std::map<std::string, TextBuffer> g_textBuffers;

char g_colorFilter[64] = "";
char g_fontFilter[64] = "";
char g_fontPath[260] = "";
char g_imagePath[260] = "";
char g_profileName[64] = "";
char g_themeName[64] = "";
std::string g_selectedProfile;
std::string g_selectedTheme;
std::string g_selectedImport;

std::vector<std::string> g_profiles;
std::vector<std::string> g_imports;
std::vector<std::string> g_images;
std::vector<std::string> g_fonts;
std::vector<std::string> g_themes;
ULONGLONG g_listsAt = 0;

// --- Helpers -------------------------------------------------------------------------------

float S() { return g_theme.scale; }

std::string Fmt(float v) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f", v);
    std::string s = buffer;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string LowerUtf8(const std::string& s) {
    std::wstring w = text::Widen(s);
    if (!w.empty()) CharLowerBuffW(&w[0], static_cast<DWORD>(w.size()));
    return text::Narrow(w);
}

bool Contains(const std::string& haystack, const char* needle) {
    if (!needle[0]) return true;
    return LowerUtf8(haystack).find(LowerUtf8(needle)) != std::string::npos;
}

std::string Stem(const std::string& file) {
    const size_t dot = file.find_last_of('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

ImU32 U32(const ImVec4& c, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

ImVec4 Alpha(ImVec4 c, float a) {
    c.w *= a;
    return c;
}

void Write(const std::string& section, const std::string& key, const std::string& value) {
    ConfigEntry e;
    e.section = section;
    e.key = key;
    e.value = value;
    QueueWrite(e);
}

void Remove(const std::string& section, const std::string& key) {
    ConfigEntry e;
    e.op = ConfigEntry::Op::Remove;
    e.section = section;
    e.key = key;
    QueueWrite(e);
}

void ThemeEdit(const ConfigEntry& entry) {
    g_lastThemeEdit = GetTickCount64();
    for (ConfigEntry& e : g_themeEdits) {
        if (e.section == entry.section && e.key == entry.key) {
            e = entry;
            return;
        }
    }
    g_themeEdits.push_back(entry);
}

void ThemeEditColors() {
    for (int i = 0; i < ColorCount(); ++i) ThemeEdit(ColorEntry(g_theme, i));
}

void FlushThemeEdits() {
    if (g_themeEdits.empty()) return;
    QueueWrites(g_themeEdits);
    g_themeEdits.clear();
}

// Colors and sizes of `look`, everything else (font, background, menu size) from the current theme.
void ApplyLook(const Theme& look) {
    g_theme.style = look.style;
    std::copy(std::begin(look.custom), std::end(look.custom), std::begin(g_theme.custom));
    ThemeEditColors();
    for (const SizeInfo& info : Sizes()) ThemeEdit(SizeEntry(g_theme, info));
}

void RefreshLists(bool force) {
    const ULONGLONG now = GetTickCount64();
    if (!force && now - g_listsAt < 1000) return;
    g_listsAt = now;
    g_profiles.clear();
    for (const std::string& f : assets::List(L"configs", ".ini")) g_profiles.push_back(Stem(f));
    g_imports.clear();
    for (const std::string& f : assets::List(L"imports", ".ini")) g_imports.push_back(Stem(f));
    g_themes.clear();
    for (const std::string& f : assets::List(L"themes", ".json")) g_themes.push_back(Stem(f));
    g_images = assets::List(L"images", ".png;.jpg;.jpeg;.bmp;.tga;.gif");
    g_fonts = assets::List(L"fonts", ".ttf;.otf;.ttc");
}

float FieldWidth() { return std::min(ImGui::GetContentRegionAvail().x * 0.55f, 280.0f * S()); }

void CopyPathButton(const char* id, const std::wstring& path) {
    const std::string utf8 = text::Narrow(path);
    if (ImGui::SmallButton(id)) ImGui::SetClipboardText(utf8.c_str());
    ImGui::SameLine();
    widgets::Hint(utf8.c_str());
}

// --- Key capture ---------------------------------------------------------------------------

bool Capturing(const std::string& id) { return g_capture.active && g_capture.id == id; }

void StartCapture(const std::string& id, const std::string& section, const std::string& key,
                  std::atomic<int> Config::*field) {
    g_capture = Capture();
    g_capture.active = true;
    g_capture.id = id;
    g_capture.section = section;
    g_capture.key = key;
    g_capture.field = field;
    g_capture.frame = ImGui::GetFrameCount();
}

void FinishCapture(int vk) {
    const std::string name = vk > 0 ? keys::Name(vk) : "NONE";
    if (!g_capture.textHotkeyId.empty()) {
        Write("TextHotkeys", g_capture.textHotkeyId, name + "|" + g_capture.textHotkeyText);
    } else {
        if (g_capture.field) (g_config.*g_capture.field) = vk;
        Write(g_capture.section, g_capture.key, name);
    }
    g_capture.active = false;
}

void Close() {
    SetMenuOpen(false);
    g_capture.active = false;
    FlushThemeEdits();
}

// Keys that the menu itself handles; everything else goes to ImGui.
bool HandleKey(const input::KeyEvent& key) {
    if (!key.down) return false;
    if (g_capture.active) {
        if (key.vk == VK_ESCAPE) {
            g_capture.active = false;
        } else if (key.vk == VK_BACK || key.vk == VK_DELETE) {
            FinishCapture(0);
        } else {
            FinishCapture(key.vk);
        }
        return true;
    }
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const bool typing = g.IO.WantTextInput;
    const bool popup = !g.OpenPopupStack.empty();
    if (key.vk == VK_ESCAPE && !typing && !popup) {
        Close();
        return true;
    }
    // In the polling fallback the worker thread toggles the menu with its key.
    const bool fallback = !hooks::HasKeyboardHook() || hooks::KeyboardFallback();
    if (key.vk == g_config.menuKey && !typing && !fallback) {
        Close();
        return true;
    }
    return false;
}

// --- Setting widgets -----------------------------------------------------------------------

bool BoolSetting(const char* label, const char* section, const char* key, std::atomic<bool> Config::*field) {
    bool v = (g_config.*field).load();
    if (!ImGui::Checkbox(label, &v)) return false;
    (g_config.*field) = v;
    Write(section, key, v ? "1" : "0");
    return true;
}

void FloatSetting(const char* label, const char* id, const char* section, const char* key,
                  std::atomic<float> Config::*field, float min, float max, const char* format,
                  ImGuiSliderFlags flags = 0, const char* help = nullptr) {
    float v = (g_config.*field).load();
    widgets::RowLabel(label, FieldWidth(), help);
    if (ImGui::SliderFloat(id, &v, min, max, format, flags | ImGuiSliderFlags_AlwaysClamp)) (g_config.*field) = v;
    if (ImGui::IsItemDeactivatedAfterEdit()) Write(section, key, Fmt(v));
}

void KeySetting(const char* label, const char* section, const char* key, std::atomic<int> Config::*field) {
    const std::string id = std::string("##bind.") + section + "." + key;
    widgets::RowLabel(label, FieldWidth());
    const bool waiting = Capturing(id);
    if (widgets::KeyButton(id.c_str(), (g_config.*field).load(), waiting, FieldWidth())) {
        if (waiting) {
            g_capture.active = false;
        } else {
            StartCapture(id, section, key, field);
        }
    }
    if (ImGui::IsItemHovered()) g_captureButtonHovered = true;
}

// --- Modules -------------------------------------------------------------------------------

void AutoSprintSettings() {
    KeySetting("Клавиша «вперёд» в игре", "AutoSprint", "ForwardKey", &Config::forwardKey);
    KeySetting("Клавиша бега в игре", "AutoSprint", "SprintKey", &Config::sprintKey);
    BoolSetting("Запасной режим через SendInput##sprint.fallback", "AutoSprint", "FallbackSendInput",
                &Config::sprintFallbackSendInput);
    widgets::Help("Если перехват клавиатуры недоступен, бег нажимается системным вводом.");
}

void ZoomSettings() {
    int mode = g_config.zoomToggle ? 1 : 0;
    const char* modes[] = {"Удерживать клавишу", "Нажать для вкл/выкл"};
    widgets::RowLabel("Режим", FieldWidth());
    if (ImGui::Combo("##zoom.mode", &mode, modes, 2)) {
        g_config.zoomToggle = mode == 1;
        Write("Zoom", "Toggle", mode == 1 ? "1" : "0");
    }
    FloatSetting("Приближение", "##zoom.factor", "Zoom", "Factor", &Config::zoomFactor, g_config.zoomMinFactor,
                 g_config.zoomMaxFactor, "x%.1f", ImGuiSliderFlags_Logarithmic);
    BoolSetting("Колесо мыши меняет приближение##zoom.scroll", "Zoom", "ScrollAdjust", &Config::zoomScrollAdjust);
    if (g_config.zoomScrollAdjust) {
        FloatSetting("Шаг колеса", "##zoom.step", "Zoom", "ScrollStep", &Config::zoomScrollStep, 1.05f, 3.0f, "x%.2f");
        BoolSetting("Запоминать приближение колеса##zoom.remember", "Zoom", "RememberScroll",
                    &Config::zoomRememberScroll);
    }
    BoolSetting("Плавная анимация##zoom.smooth", "Zoom", "Smooth", &Config::zoomSmooth);
    if (g_config.zoomSmooth) {
        FloatSetting("Скорость анимации", "##zoom.speed", "Zoom", "SmoothSpeed", &Config::zoomSmoothSpeed, 1.0f, 40.0f,
                     "%.0f");
    }
    BoolSetting("Увеличивать руку##zoom.hand", "Zoom", "ZoomHand", &Config::zoomHand);
    FloatSetting("Минимум", "##zoom.min", "Zoom", "MinFactor", &Config::zoomMinFactor, 1.0f, 10.0f, "x%.1f");
    FloatSetting("Максимум", "##zoom.max", "Zoom", "MaxFactor", &Config::zoomMaxFactor, 2.0f, 100.0f, "x%.0f",
                 ImGuiSliderFlags_Logarithmic);
}

void FullbrightSettings() {
    FloatSetting("Яркость (Gamma)", "##fullbright.gamma", "Fullbright", "Gamma", &Config::fullbrightGamma, 1.0f, 50.0f,
                 "%.1f", 0, "Ползунок яркости в игре доходит только до 1.0; 25 - как Fullbright в других клиентах.");
}

TextBuffer& Buffer(const std::string& id, const std::string& value, const char* widgetLabel) {
    TextBuffer& b = g_textBuffers[id];
    const uint64_t version = g_snapshot ? g_snapshot->version : 0;
    if (b.version != version && ImGui::GetActiveID() != ImGui::GetID(widgetLabel)) {
        snprintf(b.text.data(), b.text.size(), "%s", value.c_str());
        b.version = version;
    }
    return b;
}

void TextHotkeySettings() {
    FloatSetting("Пауза между сообщениями, с", "##th.cooldown", "TextHotkey", "Cooldown", &Config::textHotkeyCooldown,
                 0.0f, 10.0f, "%.1f");
    widgets::Hint(("Текст, который начинается с префикса команд (" + config::Prefix() +
                   "), выполняется как команда мода.").c_str());

    const std::vector<TextHotkeyEntry> entries = g_snapshot ? g_snapshot->textHotkeys : std::vector<TextHotkeyEntry>();
    int next = 1;
    for (const TextHotkeyEntry& e : entries) {
        next = std::max(next, std::atoi(e.id.c_str()) + 1);
        ImGui::PushID(e.id.c_str());
        const std::string keyId = "##thkey." + e.id;
        const bool waiting = Capturing(keyId);
        if (widgets::KeyButton(keyId.c_str(), e.key, waiting, 110.0f * S())) {
            if (waiting) {
                g_capture.active = false;
            } else {
                StartCapture(keyId, "", "", nullptr);
                g_capture.textHotkeyId = e.id;
                g_capture.textHotkeyText = e.text;
            }
        }
        if (ImGui::IsItemHovered()) g_captureButtonHovered = true;
        ImGui::SameLine();
        const std::string textId = "##thtext." + e.id;
        TextBuffer& buffer = Buffer("th." + e.id, e.text, textId.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint(textId.c_str(), "текст или команда", buffer.text.data(), buffer.text.size());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Write("TextHotkeys", e.id, keys::Name(e.key) + "|" + buffer.text.data());
        }
        ImGui::SameLine();
        if (ImGui::Button(("X##thdel." + e.id).c_str(), ImVec2(ImGui::GetFrameHeight(), 0))) Remove("TextHotkeys", e.id);
        ImGui::PopID();
    }
    if (entries.empty()) ImGui::TextDisabled("Пока нет ни одной клавиши.");
    if (ImGui::Button("+ Добавить клавишу##th.add")) Write("TextHotkeys", std::to_string(next), "NONE|");
}

void ChatSettings() {
    const std::string prefix = config::Prefix();
    TextBuffer& buffer = Buffer("chat.prefix", prefix, "##chat.prefix");
    widgets::RowLabel("Префикс команд", FieldWidth());
    ImGui::InputText("##chat.prefix", buffer.text.data(), 9);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::string value = text::Trim(buffer.text.data(), " \t");
        if (!value.empty() && value.find_first_of("/ \t") == std::string::npos) {
            Write("Chat", "Prefix", value);
        } else {
            snprintf(buffer.text.data(), buffer.text.size(), "%s", prefix.c_str());
        }
    }
    KeySetting("Открыть чат (как в игре)", "Chat", "OpenKey", &Config::chatOpenKey);
    KeySetting("Открыть команды (как в игре)", "Chat", "CommandKey", &Config::chatCommandKey);
}

struct Module {
    const char* id;  // widget ids: ##toggle.<id>, ##settings.<id>
    const char* name;
    const char* description;
    const char* section;
    std::atomic<bool> Config::*enabled;
    const char* enabledKey;
    const char* bindLabel;  // nullptr = no key
    const char* bindKey;
    std::atomic<int> Config::*bind;
    void (*settings)();
};

const Module kModules[] = {
    {"autosprint", "AutoSprint", "Пока держите клавишу «вперёд», персонаж бежит.", "AutoSprint", &Config::sprintEnabled,
     "Enabled", "Вкл/выкл", "ToggleKey", &Config::sprintToggleKey, &AutoSprintSettings},
    {"zoom", "Zoom", "Приближение с плавной анимацией, колесо мыши меняет силу. Рука не увеличивается.", "Zoom",
     &Config::zoomEnabled, "Enabled", "Клавиша", "Key", &Config::zoomKey, &ZoomSettings},
    {"fullbright", "Fullbright", "Максимальная яркость: видно в пещерах и ночью.", "Fullbright",
     &Config::fullbrightEnabled, "Enabled", "Вкл/выкл", "ToggleKey", &Config::fullbrightToggleKey, &FullbrightSettings},
    {"texthotkey", "TextHotkey", "Клавиши, которые отправляют текст в чат или выполняют команды мода.", "TextHotkey",
     &Config::textHotkeyEnabled, "Enabled", "Вкл/выкл", "ToggleKey", &Config::textHotkeyToggleKey, &TextHotkeySettings},
    {"chat", "Команды в чате", "Сообщения с префиксом выполняются модом и не уходят на сервер.", "Chat",
     &Config::chatCommands, "Commands", nullptr, nullptr, nullptr, &ChatSettings},
};

void DrawCard(const Module& m) {
    const float s = S();
    const bool enabled = (g_config.*m.enabled).load();
    const bool hovered = g_cardHovered[m.id];
    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_theme.custom[hovered ? kColCardBgHovered : kColCardBg]);
    ImGui::PushStyleColor(ImGuiCol_Border,
                          enabled ? Alpha(g_theme.custom[kColAccent], 0.6f) : ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * s, 12.0f * s));
    ImGui::BeginChild((std::string("##card.") + m.id).c_str(), ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // Title + switch
    const float toggleWidth = ImGui::GetFrameHeight() * 0.85f * 1.9f;
    const float rowStart = ImGui::GetCursorPosX();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(m.name);
    ImGui::PopFont();
    ImGui::SameLine(rowStart + rowWidth - toggleWidth);
    bool on = enabled;
    if (widgets::Toggle((std::string("##toggle.") + m.id).c_str(), &on, g_theme)) {
        (g_config.*m.enabled) = on;
        Write(m.section, m.enabledKey, on ? "1" : "0");
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", m.description);
    ImGui::PopStyleColor();

    // Key + settings button
    if (m.bind) {
        const std::string id = std::string("##bind.") + m.section + "." + m.bindKey;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", m.bindLabel);
        ImGui::SameLine();
        const bool waiting = Capturing(id);
        if (widgets::KeyButton(id.c_str(), (g_config.*m.bind).load(), waiting, 0.0f)) {
            if (waiting) {
                g_capture.active = false;
            } else {
                StartCapture(id, m.section, m.bindKey, m.bind);
            }
        }
        if (ImGui::IsItemHovered()) g_captureButtonHovered = true;
        ImGui::SameLine();
    }
    const bool expanded = g_expanded.count(m.id) != 0;
    const std::string settingsLabel = std::string(expanded ? "Скрыть" : "Настройки") + "##settings." + m.id;
    const float buttonWidth = ImGui::CalcTextSize(expanded ? "Скрыть" : "Настройки").x + ImGui::GetStyle().FramePadding.x * 2;
    const float x = rowStart + rowWidth - buttonWidth;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    if (ImGui::Button(settingsLabel.c_str())) {
        if (expanded) {
            g_expanded.erase(m.id);
        } else {
            g_expanded.insert(m.id);
        }
    }
    if (expanded) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushID(m.id);
        m.settings();
        ImGui::PopID();
    }

    ImGui::EndChild();
    g_cardHovered[m.id] = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::PopStyleColor(2);
}

void ModulesPage() {
    const int columns = ImGui::GetContentRegionAvail().x > 600.0f * S() ? 2 : 1;
    if (ImGui::BeginTable("##modules", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (const Module& m : kModules) {
            ImGui::TableNextColumn();
            DrawCard(m);
        }
        ImGui::EndTable();
    }
}

// --- Binds ---------------------------------------------------------------------------------

struct Bind {
    const char* label;
    const char* section;
    const char* key;
    std::atomic<int> Config::*field;
    bool action;  // a mod action (conflicts matter), not one of the game's own keys
};

const Bind kBinds[] = {
    {"Открыть меню", "Menu", "Key", &Config::menuKey, true},
    {"AutoSprint: вкл/выкл", "AutoSprint", "ToggleKey", &Config::sprintToggleKey, true},
    {"Zoom: приближение", "Zoom", "Key", &Config::zoomKey, true},
    {"Fullbright: вкл/выкл", "Fullbright", "ToggleKey", &Config::fullbrightToggleKey, true},
    {"TextHotkey: вкл/выкл", "TextHotkey", "ToggleKey", &Config::textHotkeyToggleKey, true},
    {"Выгрузить мод", "General", "UnloadKey", &Config::unloadKey, true},
    {"Вперёд (клавиша игры)", "AutoSprint", "ForwardKey", &Config::forwardKey, false},
    {"Бег (клавиша игры)", "AutoSprint", "SprintKey", &Config::sprintKey, false},
    {"Открыть чат (клавиша игры)", "Chat", "OpenKey", &Config::chatOpenKey, false},
    {"Открыть команды (клавиша игры)", "Chat", "CommandKey", &Config::chatCommandKey, false},
};

void BindsPage() {
    widgets::Hint("Нажмите на клавишу справа, затем новую клавишу. Esc - отмена, Backspace - убрать.");
    ImGui::Spacing();

    std::map<int, int> uses;  // key -> number of mod actions on it
    for (const Bind& b : kBinds) {
        if (b.action && (g_config.*b.field) > 0) ++uses[(g_config.*b.field).load()];
    }
    const std::vector<TextHotkeyEntry> entries = g_snapshot ? g_snapshot->textHotkeys : std::vector<TextHotkeyEntry>();
    for (const TextHotkeyEntry& e : entries) {
        if (e.key > 0) ++uses[e.key];
    }

    const float s = S();
    if (!ImGui::BeginTable("##binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        return;
    }
    ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Клавиша", ImGuiTableColumnFlags_WidthFixed, 170.0f * s);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableHeadersRow();

    auto conflict = [&](int vk) {
        if (vk <= 0 || uses[vk] < 2) return;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "(!)");
        ImGui::SetItemTooltip("Эта клавиша назначена нескольким действиям");
    };

    for (const Bind& b : kBinds) {
        ImGui::PushID(b.label);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (b.action) {
            ImGui::TextUnformatted(b.label);
        } else {
            ImGui::TextDisabled("%s", b.label);
        }
        const int vk = (g_config.*b.field).load();
        if (b.action) conflict(vk);
        ImGui::TableNextColumn();
        const std::string id = std::string("##bind.") + b.section + "." + b.key;
        const bool waiting = Capturing(id);
        if (widgets::KeyButton(id.c_str(), vk, waiting, -FLT_MIN)) {
            if (waiting) {
                g_capture.active = false;
            } else {
                StartCapture(id, b.section, b.key, b.field);
            }
        }
        if (ImGui::IsItemHovered()) g_captureButtonHovered = true;
        ImGui::TableNextColumn();
        if (ImGui::Button((std::string("X##clear.") + b.section + "." + b.key).c_str(), ImVec2(-FLT_MIN, 0))) {
            (g_config.*b.field) = 0;
            Write(b.section, b.key, "NONE");
        }
        ImGui::SetItemTooltip("Убрать клавишу");
        ImGui::PopID();
    }

    for (const TextHotkeyEntry& e : entries) {
        ImGui::PushID(("th" + e.id).c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        const std::string label = "TextHotkey #" + e.id + ": " + (e.text.empty() ? "(пусто)" : e.text);
        ImGui::TextUnformatted(label.c_str());
        conflict(e.key);
        ImGui::TableNextColumn();
        const std::string id = "##thbind." + e.id;
        const bool waiting = Capturing(id);
        if (widgets::KeyButton(id.c_str(), e.key, waiting, -FLT_MIN)) {
            if (waiting) {
                g_capture.active = false;
            } else {
                StartCapture(id, "", "", nullptr);
                g_capture.textHotkeyId = e.id;
                g_capture.textHotkeyText = e.text;
            }
        }
        if (ImGui::IsItemHovered()) g_captureButtonHovered = true;
        ImGui::TableNextColumn();
        if (ImGui::Button(("X##thclear." + e.id).c_str(), ImVec2(-FLT_MIN, 0))) Remove("TextHotkeys", e.id);
        ImGui::SetItemTooltip("Удалить эту TextHotkey");
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::Spacing();
    widgets::Hint("Клавиши игры должны совпадать с настройками управления в самой игре.");
}

// --- Appearance ----------------------------------------------------------------------------

void PresetButtons() {
    const float s = S();
    const std::vector<Preset>& presets = Presets();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < presets.size(); ++i) {
        const Theme look = presets[i].make();
        ImGui::PushID(static_cast<int>(i));
        if (i > 0) {
            // Wrap to the next line when the next swatch + button would not fit.
            const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetFrameHeight() +
                               4.0f * s + ImGui::CalcTextSize(presets[i].name).x + ImGui::GetStyle().FramePadding.x * 2;
            if (next < right) ImGui::SameLine();
        }
        ImGui::BeginGroup();
        widgets::Swatch("##swatch", look.custom[kColAccent], ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine(0, 4.0f * s);
        const std::string label = std::string(presets[i].name) + "##preset." + std::to_string(i);
        if (ImGui::Button(label.c_str())) ApplyLook(look);
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void ColorsTab() {
    ImGui::SeparatorText("Готовые темы");
    PresetButtons();

    ImGui::SeparatorText("Акцент");
    ImVec4 accent = g_theme.custom[kColAccent];
    widgets::RowLabel("Акцентный цвет", FieldWidth(),
                      "Меняет сразу все цвета, связанные с акцентом: переключатели, ползунки, выделение.");
    if (ImGui::ColorEdit4("##accent", &accent.x, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_DisplayHex)) {
        SetAccent(g_theme, accent);
        ThemeEditColors();
    }

    ImGui::SeparatorText("Все цвета");
    ImGui::SetNextItemWidth(FieldWidth());
    ImGui::InputTextWithHint("##colorfilter", "поиск цвета", g_colorFilter, sizeof(g_colorFilter));
    ImGui::SameLine();
    if (ImGui::Button("Сбросить цвета##resetcolors")) {
        const Theme defaults = DefaultTheme();
        std::copy(std::begin(defaults.style.Colors), std::end(defaults.style.Colors), std::begin(g_theme.style.Colors));
        std::copy(std::begin(defaults.custom), std::end(defaults.custom), std::begin(g_theme.custom));
        ThemeEditColors();
    }

    // Menu colors first, then ImGui's own.
    std::vector<int> order;
    for (int i = ImGuiCol_COUNT; i < ColorCount(); ++i) order.push_back(i);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) order.push_back(i);
    for (int index : order) {
        const std::string label = ColorLabel(index);
        const char* key = ColorKey(index);
        if (!Contains(label, g_colorFilter) && !Contains(key, g_colorFilter)) continue;
        if (index == ImGuiCol_COUNT) ImGui::TextDisabled("Элементы BedrockQoL");
        if (index == 0) ImGui::TextDisabled("Элементы ImGui");
        const std::string id = label + "##col." + key;
        if (ImGui::ColorEdit4(id.c_str(), &ColorRef(g_theme, index).x,
                              ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                  ImGuiColorEditFlags_NoInputs)) {
            ThemeEdit(ColorEntry(g_theme, index));
        }
        ImGui::SetItemTooltip("%s (config.ini: [Theme] %s)", label.c_str(), key);
    }
}

void SizesTab() {
    const float s = S();
    ImGui::SeparatorText("Меню");
    float scale = g_scaleEdit > 0.0f ? g_scaleEdit : g_theme.scale;
    widgets::RowLabel("Масштаб всего меню", FieldWidth());
    if (ImGui::SliderFloat("##scale", &scale, 0.5f, 2.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) g_scaleEdit = scale;
    if (ImGui::IsItemDeactivated()) {
        // Applied on release: changing the scale while dragging would move the slider itself.
        if (g_scaleEdit > 0.0f) {
            g_theme.scale = g_scaleEdit;
            ThemeEdit(MenuEntry("Scale", Fmt(g_theme.scale)));
            g_applyWindowSize = true;
        }
        g_scaleEdit = -1.0f;
    }
    widgets::RowLabel("Ширина окна", FieldWidth());
    ImGui::SliderFloat("##width", &g_theme.width, 500.0f, 2000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ThemeEdit(MenuEntry("Width", Fmt(std::round(g_theme.width))));
        g_applyWindowSize = true;
    }
    widgets::RowLabel("Высота окна", FieldWidth());
    ImGui::SliderFloat("##height", &g_theme.height, 350.0f, 1400.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ThemeEdit(MenuEntry("Height", Fmt(std::round(g_theme.height))));
        g_applyWindowSize = true;
    }

    ImGui::SeparatorText("Элементы");
    for (const SizeInfo& info : Sizes()) {
        float* value = SizeRef(g_theme.style, info);
        widgets::RowLabel(info.label, FieldWidth());
        const std::string id = std::string("##size.") + info.key;
        const bool changed = info.components == 1
                                 ? ImGui::SliderFloat(id.c_str(), value, info.min, info.max, info.format)
                                 : ImGui::SliderFloat2(id.c_str(), value, info.min, info.max, info.format);
        if (changed) ThemeEdit(SizeEntry(g_theme, info));
        ImGui::SetItemTooltip("config.ini: [Theme] %s", info.key);
    }
    ImGui::Spacing();
    if (ImGui::Button("Сбросить размеры##resetsizes")) {
        const Theme defaults = DefaultTheme();
        for (const SizeInfo& info : Sizes()) {
            std::copy(SizeRef(const_cast<ImGuiStyle&>(defaults.style), info),
                      SizeRef(const_cast<ImGuiStyle&>(defaults.style), info) + info.components,
                      SizeRef(g_theme.style, info));
            ThemeEdit(SizeEntry(g_theme, info));
        }
    }
    (void)s;
}

void SetFontName(const std::string& name) {
    g_theme.font = name;
    ThemeEdit(MenuEntry("Font", name));
}

void FontTab() {
    const float s = S();
    const std::string current = g_theme.font.empty() ? "Встроенный (Roboto)" : g_theme.font;
    widgets::RowLabel("Шрифт", FieldWidth());
    if (ImGui::BeginCombo("##font", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##fontfilter", "поиск", g_fontFilter, sizeof(g_fontFilter));
        if (ImGui::Selectable("Встроенный (Roboto)", g_theme.font.empty())) SetFontName("");
        if (!g_fonts.empty()) ImGui::SeparatorText("Папка fonts");
        for (const std::string& f : g_fonts) {
            if (Contains(f, g_fontFilter) && ImGui::Selectable(f.c_str(), g_theme.font == f)) SetFontName(f);
        }
        ImGui::SeparatorText("Шрифты Windows");
        for (const std::string& f : assets::SystemFonts()) {
            if (Contains(f, g_fontFilter) && ImGui::Selectable((f + "##sys").c_str(), g_theme.font == f)) {
                SetFontName(f);
            }
        }
        ImGui::EndCombo();
    }
    widgets::RowLabel("Размер", FieldWidth());
    if (ImGui::SliderFloat("##fontsize", &g_theme.fontSize, 10.0f, 40.0f, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
        ThemeEdit(MenuEntry("FontSize", Fmt(g_theme.fontSize)));
    }

    widgets::RowLabel("Файл или полный путь", FieldWidth());
    ImGui::SetNextItemWidth(FieldWidth() - ImGui::CalcTextSize("OK").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##fontpath", "MyFont.ttf", g_fontPath, sizeof(g_fontPath));
    ImGui::SameLine();
    if (ImGui::Button("OK##fontpath.apply")) SetFontName(text::Trim(g_fontPath, " \t\""));

    if (!assets::FontError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", assets::FontError().c_str());
    }
    ImGui::SeparatorText("Пример");
    ImGui::TextWrapped("Съешь же ещё этих мягких французских булок, да выпей чаю.");
    ImGui::TextWrapped("The quick brown fox jumps over the lazy dog. 0123456789");
    ImGui::Spacing();
    widgets::Hint("Свои шрифты (.ttf, .otf) кладите в папку:");
    CopyPathButton("Копировать##fontsdir", assets::Folder(L"fonts"));
    (void)s;
}

void SetBackgroundName(const std::string& name) {
    g_theme.background = name;
    ThemeEdit(MenuEntry("Background", name));
}

void BackgroundTab() {
    const std::string current = g_theme.background.empty() ? "Нет" : g_theme.background;
    widgets::RowLabel("Картинка", FieldWidth());
    if (ImGui::BeginCombo("##bg", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Нет##bg.none", g_theme.background.empty())) SetBackgroundName("");
        for (const std::string& f : g_images) {
            if (ImGui::Selectable(f.c_str(), g_theme.background == f)) SetBackgroundName(f);
        }
        ImGui::EndCombo();
    }
    widgets::RowLabel("Файл или полный путь", FieldWidth());
    ImGui::SetNextItemWidth(FieldWidth() - ImGui::CalcTextSize("OK").x - ImGui::GetStyle().FramePadding.x * 2 -
                            ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##bgpath", "wallpaper.png", g_imagePath, sizeof(g_imagePath));
    ImGui::SameLine();
    if (ImGui::Button("OK##bgpath.apply")) SetBackgroundName(text::Trim(g_imagePath, " \t\""));

    int target = g_theme.backgroundTarget == BackgroundTarget::Screen ? 1 : 0;
    const char* targets[] = {"В окне меню", "На весь экран"};
    widgets::RowLabel("Где показывать", FieldWidth());
    if (ImGui::Combo("##bg.target", &target, targets, 2)) {
        g_theme.backgroundTarget = target == 1 ? BackgroundTarget::Screen : BackgroundTarget::Window;
        ThemeEdit(MenuEntry("BackgroundTarget", BackgroundTargetName(g_theme.backgroundTarget)));
    }
    int mode = static_cast<int>(g_theme.backgroundMode);
    const char* modes[] = {"Заполнить (обрезать края)", "Вписать целиком", "Растянуть", "По центру", "Плиткой"};
    widgets::RowLabel("Размещение", FieldWidth());
    if (ImGui::Combo("##bg.mode", &mode, modes, 5)) {
        g_theme.backgroundMode = static_cast<BackgroundMode>(mode);
        ThemeEdit(MenuEntry("BackgroundMode", BackgroundModeName(g_theme.backgroundMode)));
    }
    widgets::RowLabel("Непрозрачность", FieldWidth());
    if (ImGui::SliderFloat("##bg.opacity", &g_theme.backgroundOpacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        ThemeEdit(MenuEntry("BackgroundOpacity", Fmt(g_theme.backgroundOpacity)));
    }
    if (ImGui::Checkbox("Затемнять игру за меню##dim", &g_theme.dimScreen)) {
        ThemeEdit(MenuEntry("DimScreen", g_theme.dimScreen ? "1" : "0"));
    }
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##dimcolor", &g_theme.custom[kColScreenDim].x,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        ThemeEdit(ColorEntry(g_theme, ImGuiCol_COUNT + kColScreenDim));
    }

    if (!assets::BackgroundError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", assets::BackgroundError().c_str());
    }
    ImTextureRef texture;
    ImVec2 size;
    if (assets::Background(texture, size)) {
        const float w = std::min(ImGui::GetContentRegionAvail().x, 240.0f * S());
        ImGui::Image(texture, ImVec2(w, w * size.y / std::max(size.x, 1.0f)));
        ImGui::TextDisabled("%.0f x %.0f", size.x, size.y);
    }
    ImGui::Spacing();
    widgets::Hint("Картинки (PNG, JPG, BMP, TGA, GIF) кладите в папку:");
    CopyPathButton("Копировать##imagesdir", assets::Folder(L"images"));
}

void ThemesTab() {
    ImGui::SeparatorText("Встроенные");
    PresetButtons();

    ImGui::SeparatorText("Сохранённые темы");
    const float s = S();
    if (ImGui::BeginListBox("##themes", ImVec2(-FLT_MIN, 6.5f * ImGui::GetTextLineHeightWithSpacing()))) {
        for (const std::string& name : g_themes) {
            if (ImGui::Selectable((name + "##theme." + name).c_str(), g_selectedTheme == name)) g_selectedTheme = name;
        }
        if (g_themes.empty()) ImGui::TextDisabled("Нет сохранённых тем");
        ImGui::EndListBox();
    }
    ImGui::BeginDisabled(g_selectedTheme.empty());
    if (ImGui::Button("Применить##theme.load")) {
        FlushThemeEdits();
        QueueCommand("theme load " + g_selectedTheme);
    }
    ImGui::SameLine();
    if (ImGui::Button("Удалить##theme.delete")) {
        QueueCommand("theme delete " + g_selectedTheme);
        g_selectedTheme.clear();
        g_listsAt = 0;
    }
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(FieldWidth());
    ImGui::InputTextWithHint("##themename", "название темы", g_themeName, sizeof(g_themeName));
    ImGui::SameLine();
    const std::string name = text::Trim(g_themeName, " \t");
    ImGui::BeginDisabled(name.empty());
    if (ImGui::Button("Сохранить текущую##theme.save")) {
        FlushThemeEdits();
        QueueCommand("theme save " + name);
        g_listsAt = 0;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Сбросить внешний вид к стандартному##theme.reset")) {
        g_themeEdits.clear();
        QueueCommand("theme reset");
    }
    ImGui::Spacing();
    widgets::Hint("Темы - файлы JSON (цвета, размеры, шрифт, фон). Ими можно делиться: положите файл в папку");
    CopyPathButton("Копировать##themesdir", assets::Folder(L"themes"));
    (void)s;
}

void AppearancePage() {
    if (!ImGui::BeginTabBar("##appearance")) return;
    if (ImGui::BeginTabItem("Цвета##tab.colors")) {
        ColorsTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Размеры##tab.sizes")) {
        SizesTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Шрифт##tab.font")) {
        FontTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Фон##tab.background")) {
        BackgroundTab();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Темы##tab.themes")) {
        ThemesTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

// --- Configs -------------------------------------------------------------------------------

void ConfigsPage() {
    widgets::Hint("Конфиг - копия всех настроек: модули, бинды, TextHotkey и внешний вид меню.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(FieldWidth());
    ImGui::InputTextWithHint("##cfgname", "название нового конфига", g_profileName, sizeof(g_profileName));
    ImGui::SameLine();
    const std::string newName = text::Trim(g_profileName, " \t");
    ImGui::BeginDisabled(newName.empty());
    if (ImGui::Button("Сохранить##cfgsave")) {
        FlushThemeEdits();
        QueueCommand("config save " + newName);
        g_selectedProfile = newName;
        g_profileName[0] = '\0';
        g_listsAt = 0;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Сохранённые конфиги");
    if (ImGui::BeginListBox("##configs", ImVec2(-FLT_MIN, 7.5f * ImGui::GetTextLineHeightWithSpacing()))) {
        for (const std::string& name : g_profiles) {
            if (ImGui::Selectable((name + "##cfg." + name).c_str(), g_selectedProfile == name)) g_selectedProfile = name;
        }
        if (g_profiles.empty()) ImGui::TextDisabled("Пока нет сохранённых конфигов");
        ImGui::EndListBox();
    }
    const bool selected = std::find(g_profiles.begin(), g_profiles.end(), g_selectedProfile) != g_profiles.end();
    ImGui::BeginDisabled(!selected);
    if (ImGui::Button("Загрузить##cfgload")) {
        g_themeEdits.clear();
        QueueCommand("config load " + g_selectedProfile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Перезаписать текущими##cfgoverwrite")) {
        FlushThemeEdits();
        QueueCommand("config save " + g_selectedProfile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Экспорт##cfgexport")) QueueCommand("config export " + g_selectedProfile);
    ImGui::SameLine();
    if (ImGui::Button("Удалить##cfgdelete")) ImGui::OpenPopup("##cfgconfirm");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##cfgconfirm")) {
        ImGui::Text("Удалить конфиг «%s»?", g_selectedProfile.c_str());
        if (ImGui::Button("Удалить##cfgdelete.yes")) {
            QueueCommand("config delete " + g_selectedProfile);
            g_selectedProfile.clear();
            g_listsAt = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Отмена##cfgdelete.no")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Импорт");
    if (g_imports.empty()) {
        widgets::Hint("Файлы .ini для импорта кладите в папку imports (или используйте меню лаунчера).");
    } else {
        ImGui::SetNextItemWidth(FieldWidth());
        if (ImGui::BeginCombo("##imports", g_selectedImport.empty() ? "выберите файл" : g_selectedImport.c_str())) {
            for (const std::string& name : g_imports) {
                if (ImGui::Selectable(name.c_str(), g_selectedImport == name)) g_selectedImport = name;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(g_selectedImport.empty());
        if (ImGui::Button("Импортировать##cfgimport")) {
            g_themeEdits.clear();
            QueueCommand("config import " + g_selectedImport);
            g_listsAt = 0;
        }
        ImGui::EndDisabled();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Папка конфигов:");
    CopyPathButton("Копировать##configsdir", assets::Folder(L"configs"));
}

// --- General -------------------------------------------------------------------------------

void MenuBool(const char* label, bool Theme::*field, const char* key) {
    if (ImGui::Checkbox(label, &(g_theme.*field))) ThemeEdit(MenuEntry(key, g_theme.*field ? "1" : "0"));
}

void GeneralPage() {
    ImGui::SeparatorText("Поведение");
    BoolSetting("Функции работают только в мире##general.hiddencursor", "General", "RequireHiddenCursor",
                &Config::requireHiddenCursor);
    widgets::Help("Зум, бег и бинды срабатывают, только когда курсор скрыт (нет чата, инвентаря и меню игры). "
                  "Выключите, если функции не включаются.");
    MenuBool("Уведомления в игре##general.notifications", &Theme::notifications, "Notifications");
    MenuBool("Плавное появление меню##general.animations", &Theme::animations, "Animations");
    KeySetting("Открыть меню", "Menu", "Key", &Config::menuKey);
    KeySetting("Выгрузить мод", "General", "UnloadKey", &Config::unloadKey);

    ImGui::SeparatorText("Действия");
    if (ImGui::Button("Перечитать config.ini##general.reload")) QueueCommand("config reload");
    ImGui::SameLine();
    if (ImGui::Button("Выгрузить мод##general.unload")) QueueCommand("unload");

    ImGui::SeparatorText("Информация");
    ImGui::Text("BedrockQoL %s", BEDROCKQOL_VERSION);
    ImGui::Text("Меню: %s", overlay::Status().c_str());
    ImGui::Text("Зум: %s, Fullbright: %s", hooks::HasFovHook() ? "работает" : "недоступен",
                hooks::HasGammaHook() ? "работает" : "недоступен");
    ImGui::Text("Клавиатура: %s, мышь: %s",
                hooks::HasKeyboardHook() && !hooks::KeyboardFallback() ? "перехват" : "опрос (без команд в чате)",
                hooks::HasMouseHook() ? "перехват" : "опрос");
    ImGui::Text("Список команд: %shelp в чате", config::Prefix().c_str());
    ImGui::TextDisabled("Папка настроек:");
    CopyPathButton("Копировать##datadir", config::Directory());
}

// --- Window --------------------------------------------------------------------------------

// Draws the background picture into [min, max] with the chosen placement.
void DrawBackground(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float alpha, float rounding) {
    ImTextureRef texture;
    ImVec2 image;
    if (!assets::Background(texture, image) || alpha <= 0.0f || image.x <= 0 || image.y <= 0) return;
    const ImU32 color = IM_COL32(255, 255, 255, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    const ImVec2 box(max.x - min.x, max.y - min.y);

    switch (g_theme.backgroundMode) {
        case BackgroundMode::Stretch:
            draw->AddImageRounded(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), color, rounding);
            return;
        case BackgroundMode::Fill: {
            const float scale = std::max(box.x / image.x, box.y / image.y);
            const ImVec2 uv(box.x / scale / image.x, box.y / scale / image.y);
            const ImVec2 uv0((1.0f - uv.x) * 0.5f, (1.0f - uv.y) * 0.5f);
            draw->AddImageRounded(texture, min, max, uv0, ImVec2(uv0.x + uv.x, uv0.y + uv.y), color, rounding);
            return;
        }
        case BackgroundMode::Fit: {
            const float scale = std::min(box.x / image.x, box.y / image.y);
            const ImVec2 size(image.x * scale, image.y * scale);
            const ImVec2 p(min.x + (box.x - size.x) * 0.5f, min.y + (box.y - size.y) * 0.5f);
            draw->AddImage(texture, p, ImVec2(p.x + size.x, p.y + size.y), ImVec2(0, 0), ImVec2(1, 1), color);
            return;
        }
        case BackgroundMode::Center: {
            const ImVec2 p(min.x + (box.x - image.x) * 0.5f, min.y + (box.y - image.y) * 0.5f);
            draw->PushClipRect(min, max, true);
            draw->AddImage(texture, p, ImVec2(p.x + image.x, p.y + image.y), ImVec2(0, 0), ImVec2(1, 1), color);
            draw->PopClipRect();
            return;
        }
        case BackgroundMode::Tile: {
            // The renderers clamp texture coordinates, so tiles are drawn one by one (at least 32 px).
            const float factor = std::max(1.0f, 32.0f / std::min(image.x, image.y));
            const ImVec2 tile(image.x * factor, image.y * factor);
            draw->PushClipRect(min, max, true);
            for (float y = min.y; y < max.y; y += tile.y) {
                for (float x = min.x; x < max.x; x += tile.x) {
                    draw->AddImage(texture, ImVec2(x, y), ImVec2(x + tile.x, y + tile.y), ImVec2(0, 0), ImVec2(1, 1), color);
                }
            }
            draw->PopClipRect();
            return;
        }
    }
}

void DrawSidebar(float width) {
    const float s = S();
    ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    const float pad = 16.0f * s;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    ImGui::BeginGroup();
    widgets::Heading("BedrockQoL", 1.45f, g_theme.custom[kColAccent]);
    ImGui::TextDisabled("%s", BEDROCKQOL_VERSION);
    ImGui::EndGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f * s);

    const ImVec4 accent = g_theme.custom[kColAccent];
    ImGui::PushStyleColor(ImGuiCol_Header, Alpha(accent, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Alpha(accent, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Alpha(accent, 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    for (const PageInfo& p : kPages) {
        ImGui::SetCursorPosX(pad * 0.5f);
        const bool selected = g_page == p.page;
        if (ImGui::Selectable(p.label, selected, 0, ImVec2(width - pad, 34.0f * s))) g_page = p.page;
        if (selected) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, a.y + 6.0f * s), ImVec2(a.x + 3.0f * s, b.y - 6.0f * s),
                                                      U32(accent), 2.0f * s);
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    const std::string hint = keys::Name(g_config.menuKey) + " или Esc - закрыть";
    ImGui::SetCursorPos(ImVec2(pad, ImGui::GetWindowHeight() - pad - ImGui::GetTextLineHeight()));
    ImGui::TextDisabled("%s", hint.c_str());
    ImGui::EndChild();
}

void DrawContent() {
    const float s = S();
    static Page shownPage = g_page;
    if (shownPage != g_page) {
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));  // a new page starts at the top
        shownPage = g_page;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * s, 16.0f * s));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    for (const PageInfo& p : kPages) {
        if (p.page != g_page) continue;
        widgets::Heading(p.title, 1.35f, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::TextDisabled("%s", p.subtitle);
    }
    ImGui::Spacing();
    ImGui::Spacing();
    switch (g_page) {
        case Page::Modules: ModulesPage(); break;
        case Page::Binds: BindsPage(); break;
        case Page::Appearance: AppearancePage(); break;
        case Page::Configs: ConfigsPage(); break;
        case Page::General: GeneralPage(); break;
    }
    ImGui::EndChild();
}

void DrawWindow() {
    const float s = S();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    const ImVec2 minSize(std::min(520.0f * s, display.x), std::min(340.0f * s, display.y));
    if (g_applyWindowSize) {
        const ImVec2 size(std::clamp(g_theme.width * s, minSize.x, display.x * 0.96f),
                          std::clamp(g_theme.height * s, minSize.y, display.y * 0.96f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        g_applyWindowSize = false;
        g_lastWindowSize = size;
    }
    ImGui::SetNextWindowSizeConstraints(minSize, display);
    if (!g_wasOpen) ImGui::SetNextWindowFocus();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin("BedrockQoL###menu", nullptr,
                                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (visible) {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (g_theme.backgroundTarget == BackgroundTarget::Window) {
            DrawBackground(draw, pos, ImVec2(pos.x + size.x, pos.y + size.y), g_theme.backgroundOpacity * g_fade,
                           ImGui::GetStyle().WindowRounding);
        }
        const float sidebar = 200.0f * s;
        draw->AddRectFilled(pos, ImVec2(pos.x + sidebar, pos.y + size.y), U32(g_theme.custom[kColSidebarBg], g_fade),
                            ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
        DrawSidebar(sidebar);
        ImGui::SameLine(0, 0);
        DrawContent();

        // Resized with the mouse: remember the new size once the button is released (a size clamped
        // to a small screen is not saved).
        const bool changed = std::fabs(size.x - g_lastWindowSize.x) > 0.5f || std::fabs(size.y - g_lastWindowSize.y) > 0.5f;
        if (changed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) g_userResizing = true;
        g_lastWindowSize = size;
        if (g_userResizing && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_userResizing = false;
            g_theme.width = std::round(size.x / s);
            g_theme.height = std::round(size.y / s);
            ThemeEdit(MenuEntry("Width", Fmt(g_theme.width)));
            ThemeEdit(MenuEntry("Height", Fmt(g_theme.height)));
        }
    }
    ImGui::End();
}

void DrawToasts() {
    if (g_toasts.empty()) return;
    const float s = S();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(340.0f * s, display.x * 0.5f);
    const float pad = 10.0f * s;
    const float margin = 16.0f * s;
    const ULONGLONG now = GetTickCount64();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImVec4 textColor = g_theme.style.Colors[ImGuiCol_Text];

    // Bottom-right corner, newest at the bottom, at most 5.
    float bottom = display.y - margin;
    const size_t first = g_toasts.size() > 5 ? g_toasts.size() - 5 : 0;
    for (size_t i = g_toasts.size(); i-- > first;) {
        const Toast& t = g_toasts[i];
        const float age = static_cast<float>(now - t.createdAt);
        float alpha = 1.0f;
        if (age < 150.0f) alpha = age / 150.0f;
        if (age > 4000.0f) alpha = std::max(0.0f, 1.0f - (age - 4000.0f) / 500.0f);
        const float wrap = width - pad * 2 - 4.0f * s;
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrap, t.text.c_str());
        const float slide = (1.0f - std::min(1.0f, age / 150.0f)) * 24.0f * s;
        const ImVec2 a(display.x - margin - width + slide, bottom - textSize.y - pad * 2);
        const ImVec2 b(display.x - margin + slide, bottom);
        draw->AddRectFilled(a, b, U32(g_theme.custom[kColToastBg], alpha), 8.0f * s);
        draw->AddRectFilled(a, ImVec2(a.x + 4.0f * s, b.y), U32(g_theme.custom[kColAccent], alpha), 8.0f * s,
                            ImDrawFlags_RoundCornersLeft);
        draw->AddText(font, fontSize, ImVec2(a.x + pad + 4.0f * s, a.y + pad), U32(textColor, alpha), t.text.c_str(),
                      nullptr, wrap);
        bottom = a.y - 8.0f * s;
    }
}

}  // namespace

void Init() {
    assets::Init();
    g_applyWindowSize = true;
    g_wasOpen = false;
    g_fade = 0.0f;
}

bool WantsFrame() {
    TakeToasts(g_toasts);
    if (!g_theme.notifications) g_toasts.clear();
    const ULONGLONG now = GetTickCount64();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
                                  [now](const Toast& t) { return now - t.createdAt > 4500; }),
                   g_toasts.end());
    return MenuOpen() || g_fade > 0.0f || !g_toasts.empty();
}

void BeginFrame(ImGuiIO& io) {
    const bool open = MenuOpen();
    if (open && !g_wasOpen) {
        input::Reset(io);
        RefreshLists(true);
    }

    // Settings from config.ini (changed by commands, profiles, or our own writes coming back).
    std::shared_ptr<const Snapshot> latest = Latest();
    if (!g_snapshot || latest->version != g_snapshot->version) {
        const ULONGLONG now = GetTickCount64();
        const bool ownWritesPending = latest->appliedWrite < LastWriteId();
        if (ownWritesPending && g_waitingSince == 0) g_waitingSince = now;
        const bool editing = !g_themeEdits.empty() || ImGui::GetCurrentContext()->ActiveId != 0;
        if (!g_snapshot || (!editing && (!ownWritesPending || now - g_waitingSince > 3000))) {
            const Theme previous = g_theme;
            g_theme = latest->theme;
            g_snapshot = latest;
            g_waitingSince = 0;
            if (previous.width != g_theme.width || previous.height != g_theme.height || previous.scale != g_theme.scale) {
                g_applyWindowSize = true;
            }
        }
    }

    assets::Update();
    assets::SetFont(g_theme.font);
    assets::SetBackground(g_theme.background);

    // Fade in / out.
    const float target = open ? 1.0f : 0.0f;
    if (!g_theme.animations) {
        g_fade = target;
    } else if (g_fade < target) {
        g_fade = std::min(target, g_fade + io.DeltaTime / 0.15f);
    } else if (g_fade > target) {
        g_fade = std::max(target, g_fade - io.DeltaTime / 0.12f);
    }

    ImGuiStyle style = ScaledStyle(g_theme);
    style.Alpha = std::clamp(style.Alpha * std::max(g_fade, 0.01f), 0.0f, 1.0f);
    ImGui::GetStyle() = style;
    io.MouseDrawCursor = open && game::CursorHidden();

    std::vector<input::KeyEvent> keys;
    input::Drain(io, keys);
    for (const input::KeyEvent& key : keys) {
        if (!MenuOpen()) break;  // closed by an earlier key of this batch: the rest is not for us
        if (HandleKey(key)) continue;
        input::FeedKey(io, key);
    }
    if (open) RefreshLists(false);
}

void Draw() {
    const bool open = MenuOpen();
    g_captureButtonHovered = false;
    DrawToasts();
    if (g_fade > 0.0f) {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        if (g_theme.dimScreen) {
            background->AddRectFilled(ImVec2(0, 0), io.DisplaySize, U32(g_theme.custom[kColScreenDim], g_fade));
        }
        if (g_theme.backgroundTarget == BackgroundTarget::Screen) {
            DrawBackground(background, ImVec2(0, 0), io.DisplaySize, g_theme.backgroundOpacity * g_fade, 0.0f);
        }
        DrawWindow();
    }

    // A click anywhere else cancels waiting for a key.
    if (g_capture.active && g_capture.frame != ImGui::GetFrameCount() && !g_captureButtonHovered &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        g_capture.active = false;
    }
    if (!open) g_capture.active = false;

    // Appearance edits are written once the user stops dragging for a moment.
    const bool idle = ImGui::GetCurrentContext()->ActiveId == 0 && GetTickCount64() - g_lastThemeEdit > 300;
    if (!g_themeEdits.empty() && (idle || !open)) FlushThemeEdits();
    g_wasOpen = open;
}

void Shutdown() {
    assets::Shutdown();
    g_snapshot.reset();
    g_toasts.clear();
    g_capture.active = false;
}

}  // namespace gui::menu
