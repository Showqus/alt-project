#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "keys.h"
#include "log.h"

Config g_config;

namespace config {
namespace {

const char kDefaultIni[] =
    "; BedrockQoL - AutoSprint + Zoom for Minecraft Bedrock (Windows) 1.21.5x\n"
    ";\n"
    "; Key names: A-Z, 0-9, F1-F24, CTRL, SHIFT, ALT, SPACE, TAB, CAPSLOCK, END, HOME,\n"
    "; INSERT, DELETE, PAGEUP, PAGEDOWN, NUMPAD0-9, NONE, or a virtual-key code (e.g. 0x43).\n"
    "; Restart the game (or unload + inject again) after editing this file.\n"
    "\n"
    "[General]\n"
    "; Key that unloads the DLL from the game.\n"
    "UnloadKey=END\n"
    "; 1 = only react while the mouse cursor is hidden (i.e. you are in the world,\n"
    ";     not in chat/inventory/menus). Set to 0 if features never activate.\n"
    "RequireHiddenCursor=1\n"
    "\n"
    "[AutoSprint]\n"
    "Enabled=1\n"
    "; Toggles AutoSprint on/off in game.\n"
    "ToggleKey=F8\n"
    "; Must match your in-game \"Walk Forward\" and \"Sprint\" bindings.\n"
    "ForwardKey=W\n"
    "SprintKey=CTRL\n"
    "; If the keyboard hook cannot be installed, emulate the sprint key with SendInput.\n"
    "FallbackSendInput=1\n"
    "\n"
    "[Zoom]\n"
    "Enabled=1\n"
    "Key=C\n"
    "; 0 = hold the key to zoom, 1 = press to toggle.\n"
    "Toggle=0\n"
    "; FOV is divided by this factor while zooming.\n"
    "Factor=4.0\n"
    "MinFactor=1.5\n"
    "MaxFactor=50\n"
    "; Mouse wheel changes the zoom while zooming (hotbar does not scroll).\n"
    "ScrollAdjust=1\n"
    "ScrollStep=1.25\n"
    "; 1 = keep the scrolled zoom level for the next zoom, 0 = reset to Factor.\n"
    "RememberScroll=0\n"
    "Smooth=1\n"
    "SmoothSpeed=12\n"
    "; 1 = also zoom the first-person hand.\n"
    "ZoomHand=0\n"
    "\n"
    "[Signatures]\n"
    "; Leave empty to use the built-in signatures for 1.21.5x.\n"
    "; You can paste updated IDA-style patterns here (e.g. \"48 8B ? ? 89\").\n"
    "; Patterns starting with E8/E9 are treated as call sites and resolved to the callee.\n"
    "GetFov=\n"
    "KeyboardFeed=\n"
    "MouseFeed=\n";

std::wstring g_path;

std::string ReadString(const char* section, const char* key, const std::string& def) {
    wchar_t wsection[64];
    wchar_t wkey[64];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsection, 64);
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, 64);

    wchar_t buffer[1024];
    const DWORD len = GetPrivateProfileStringW(wsection, wkey, L"\x01", buffer, 1024, g_path.c_str());
    if (len == 1 && buffer[0] == L'\x01') return def;

    char out[1024];
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, out, sizeof(out), nullptr, nullptr);

    // Strip inline comments and surrounding whitespace / quotes.
    std::string value = out;
    const size_t comment = value.find(';');
    if (comment != std::string::npos) value.erase(comment);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '"')) value.pop_back();
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '"')) ++start;
    return value.substr(start);
}

bool ReadBool(const char* section, const char* key, bool def) {
    const std::string v = ReadString(section, key, def ? "1" : "0");
    if (v.empty()) return def;
    return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y';
}

float ReadFloat(const char* section, const char* key, float def) {
    const std::string v = ReadString(section, key, "");
    if (v.empty()) return def;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end == v.c_str()) {
        logx::Warn("Config [%s] %s=%s is not a number, using %.2f", section, key, v.c_str(), def);
        return def;
    }
    return f;
}

int ReadKey(const char* section, const char* key, int def) {
    const std::string v = ReadString(section, key, keys::Name(def));
    const int vk = keys::Parse(v);
    if (vk < 0) {
        logx::Warn("Config [%s] %s=%s is not a known key, using %s", section, key, v.c_str(), keys::Name(def).c_str());
        return def;
    }
    return vk;
}

void WriteDefaultFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) {
        logx::Warn("Could not create default config file, using built-in defaults");
        return;
    }
    fwrite(kDefaultIni, 1, sizeof(kDefaultIni) - 1, f);
    fclose(f);
    logx::Info("Created default config file");
}

}  // namespace

void Load(const std::wstring& path) {
    g_path = path;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) WriteDefaultFile(path);

    Config& c = g_config;
    const Config d;  // defaults

    c.unloadKey = ReadKey("General", "UnloadKey", d.unloadKey);
    c.requireHiddenCursor = ReadBool("General", "RequireHiddenCursor", d.requireHiddenCursor);

    c.sprintEnabled = ReadBool("AutoSprint", "Enabled", d.sprintEnabled);
    c.sprintToggleKey = ReadKey("AutoSprint", "ToggleKey", d.sprintToggleKey);
    c.forwardKey = ReadKey("AutoSprint", "ForwardKey", d.forwardKey);
    c.sprintKey = ReadKey("AutoSprint", "SprintKey", d.sprintKey);
    c.sprintFallbackSendInput = ReadBool("AutoSprint", "FallbackSendInput", d.sprintFallbackSendInput);

    c.zoomEnabled = ReadBool("Zoom", "Enabled", d.zoomEnabled);
    c.zoomKey = ReadKey("Zoom", "Key", d.zoomKey);
    c.zoomToggle = ReadBool("Zoom", "Toggle", d.zoomToggle);
    c.zoomFactor = ReadFloat("Zoom", "Factor", d.zoomFactor);
    c.zoomMinFactor = ReadFloat("Zoom", "MinFactor", d.zoomMinFactor);
    c.zoomMaxFactor = ReadFloat("Zoom", "MaxFactor", d.zoomMaxFactor);
    c.zoomScrollAdjust = ReadBool("Zoom", "ScrollAdjust", d.zoomScrollAdjust);
    c.zoomScrollStep = ReadFloat("Zoom", "ScrollStep", d.zoomScrollStep);
    c.zoomRememberScroll = ReadBool("Zoom", "RememberScroll", d.zoomRememberScroll);
    c.zoomSmooth = ReadBool("Zoom", "Smooth", d.zoomSmooth);
    c.zoomSmoothSpeed = ReadFloat("Zoom", "SmoothSpeed", d.zoomSmoothSpeed);
    c.zoomHand = ReadBool("Zoom", "ZoomHand", d.zoomHand);

    c.sigGetFov = ReadString("Signatures", "GetFov", "");
    c.sigKeyboardFeed = ReadString("Signatures", "KeyboardFeed", "");
    c.sigMouseFeed = ReadString("Signatures", "MouseFeed", "");

    // Keep the numbers sane so a typo cannot produce a 0 or negative FOV.
    if (c.zoomMinFactor < 1.0f) c.zoomMinFactor = 1.0f;
    if (c.zoomMaxFactor < c.zoomMinFactor) c.zoomMaxFactor = c.zoomMinFactor;
    if (c.zoomFactor < c.zoomMinFactor) c.zoomFactor = c.zoomMinFactor;
    if (c.zoomFactor > c.zoomMaxFactor) c.zoomFactor = c.zoomMaxFactor;
    if (c.zoomScrollStep <= 1.0f) c.zoomScrollStep = 1.25f;
    if (c.zoomSmoothSpeed <= 0.0f) c.zoomSmooth = false;

    logx::Info("Config: AutoSprint=%d (toggle %s, forward %s, sprint %s), Zoom=%d (key %s, %s, x%.2f), unload %s",
               c.sprintEnabled, keys::Name(c.sprintToggleKey).c_str(), keys::Name(c.forwardKey).c_str(),
               keys::Name(c.sprintKey).c_str(), c.zoomEnabled, keys::Name(c.zoomKey).c_str(),
               c.zoomToggle ? "toggle" : "hold", c.zoomFactor, keys::Name(c.unloadKey).c_str());
}

}  // namespace config
