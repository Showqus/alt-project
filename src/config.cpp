#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

#include "gui/state.h"
#include "keys.h"
#include "log.h"
#include "text.h"

Config g_config;

namespace config {
namespace {

const char kDefaultIni[] =
    "; BedrockQoL - AutoSprint, Zoom, Fullbright, TextHotkey and chat commands\n"
    "; for Minecraft Bedrock (Windows) 1.21.5x.\n"
    ";\n"
    "; Key names: A-Z, 0-9, F1-F24, CTRL, SHIFT, ALT, SPACE, TAB, CAPSLOCK, END, HOME,\n"
    "; INSERT, DELETE, PAGEUP, PAGEDOWN, NUMPAD0-9, SLASH, GRAVE, NONE, or a virtual-key code (0x43).\n"
    "; Most settings can also be changed in game through chat commands (type .help in chat).\n"
    "\n"
    "[General]\n"
    "; Key that unloads the DLL from the game.\n"
    "UnloadKey=END\n"
    "; 1 = only react while the mouse cursor is hidden (i.e. you are in the world,\n"
    ";     not in chat/inventory/menus). Set to 0 if features never activate.\n"
    "RequireHiddenCursor=1\n"
    "\n"
    "[Menu]\n"
    "; In-game menu (modules, key binds, appearance, configs). Also opens with the .menu command.\n"
    "Key=INSERT\n"
    "; Font file: empty = built-in (Roboto), a name from BedrockQoL\\fonts or C:\\Windows\\Fonts\n"
    "; (e.g. segoeui.ttf), or a full path. Size in pixels before Scale.\n"
    "Font=\n"
    "FontSize=17\n"
    "; Size of the whole menu (0.5 - 2.5).\n"
    "Scale=1.0\n"
    "Width=860\n"
    "Height=560\n"
    "; Background picture: a PNG/JPG/BMP/TGA/GIF from BedrockQoL\\images (or a full path).\n"
    "; Mode: fill, fit, stretch, center, tile. Target: window (inside the menu) or screen.\n"
    "Background=\n"
    "BackgroundMode=fill\n"
    "BackgroundTarget=window\n"
    "BackgroundOpacity=0.35\n"
    "; Darken the game behind the menu (color: [Theme] ScreenDim).\n"
    "DimScreen=1\n"
    "; Show command results and toggles as notifications inside the game.\n"
    "Notifications=1\n"
    "; Fade the menu in and out.\n"
    "Animations=1\n"
    "\n"
    "[Theme]\n"
    "; Colors (#RRGGBBAA) and sizes of the menu. Only values that differ from the built-in theme are\n"
    "; stored here; the menu (Appearance tab) edits them. Delete the section to reset the theme.\n"
    "\n"
    "[Chat]\n"
    "; Chat commands such as .bind / .toggle / .config. The message is not sent to the server.\n"
    "Commands=1\n"
    "; Prefix for chat commands. Change it in game with: .prefix ;\n"
    "Prefix=.\n"
    "; Must match the in-game \"Open chat\" / \"Open command\" keys.\n"
    "OpenKey=T\n"
    "CommandKey=SLASH\n"
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
    "[Fullbright]\n"
    "Enabled=0\n"
    "ToggleKey=NONE\n"
    "; Brightness used while Fullbright is on (the in-game slider maxes out at 1.0).\n"
    "Gamma=25\n"
    "\n"
    "[TextHotkey]\n"
    "Enabled=1\n"
    "ToggleKey=NONE\n"
    "; Minimum seconds between two messages (servers kick for spam).\n"
    "Cooldown=1.0\n"
    "\n"
    "[TextHotkeys]\n"
    "; <number>=<KEY>|<text>. Text starting with the chat prefix runs as a command.\n"
    "; Add them in game with: .th add F6 gg\n"
    "\n"
    "[Signatures]\n"
    "; Leave empty to use the built-in signatures for 1.21.5x.\n"
    "; You can paste updated IDA-style patterns here (e.g. \"48 8B ? ? 89\").\n"
    "; Patterns starting with E8/E9 are treated as call sites and resolved to the callee.\n"
    "GetFov=\n"
    "KeyboardFeed=\n"
    "MouseFeed=\n"
    "GetGamma=\n";

std::wstring g_dir;
std::wstring g_path;

SRWLOCK g_prefixLock = SRWLOCK_INIT;
std::string g_prefix = ".";

std::string ReadRaw(const std::string& section, const std::string& key, const std::string& def) {
    const std::wstring wsection = text::Widen(section);
    const std::wstring wkey = text::Widen(key);
    wchar_t buffer[2048];
    const DWORD len =
        GetPrivateProfileStringW(wsection.c_str(), wkey.c_str(), L"\x01", buffer, 2048, g_path.c_str());
    if (len == 1 && buffer[0] == L'\x01') return def;
    return text::Narrow(buffer);
}

std::string ReadString(const char* section, const char* key, const std::string& def) {
    std::string value = ReadRaw(section, key, def);
    // Strip inline comments and surrounding whitespace / quotes.
    const size_t comment = value.find(" ;");
    if (comment != std::string::npos) value.erase(comment);
    return text::Trim(value, " \t\"");
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

std::vector<TextHotkeyEntry> ReadTextHotkeys() {
    std::vector<TextHotkeyEntry> entries;
    std::vector<wchar_t> buffer(32768);
    const DWORD len = GetPrivateProfileSectionW(L"TextHotkeys", buffer.data(), static_cast<DWORD>(buffer.size()),
                                                g_path.c_str());
    for (const wchar_t* p = buffer.data(); p < buffer.data() + len && *p; p += wcslen(p) + 1) {
        const std::string line = text::Narrow(p);
        if (line.empty() || line[0] == ';') continue;
        const size_t eq = line.find('=');
        const size_t bar = line.find('|', eq == std::string::npos ? 0 : eq);
        if (eq == std::string::npos || bar == std::string::npos) continue;

        TextHotkeyEntry entry;
        entry.id = text::Trim(line.substr(0, eq), " \t");
        entry.key = keys::Parse(line.substr(eq + 1, bar - eq - 1));
        entry.text = line.substr(bar + 1);
        // Entries without a key or text (just added in the menu) are kept but never fire.
        if (entry.key < 0 || entry.id.empty()) {
            logx::Warn("Config [TextHotkeys] %s: bad entry '%s'", entry.id.c_str(), line.c_str());
            continue;
        }
        entries.push_back(entry);
    }
    return entries;
}

// UTF-16LE with BOM: the Windows profile APIs then read and write any Unicode text (Cyrillic
// TextHotkeys, emoji) instead of converting it to the ANSI code page.
void WriteDefaultFile() {
    FILE* f = _wfopen(g_path.c_str(), L"wb");
    if (!f) {
        logx::Warn("Could not create default config file, using built-in defaults");
        return;
    }
    const std::wstring content = L"\xFEFF" + text::Widen(kDefaultIni);
    fwrite(content.data(), sizeof(wchar_t), content.size(), f);
    fclose(f);
    logx::Info("Created default config file");
}

// Converts an ANSI / UTF-8 config file (older version, imported profile, edited in another
// editor) to UTF-16LE so Unicode values survive WritePrivateProfileStringW.
void EnsureUtf16(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string bytes;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) bytes.append(buffer, n);
    fclose(f);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        return;
    }
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);

    UINT codePage = CP_UTF8;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (len == 0 && !bytes.empty()) {
        codePage = CP_ACP;
        len = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }
    std::wstring wide(static_cast<size_t>(len), L'\0');
    if (len > 0) MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), &wide[0], len);
    wide.insert(wide.begin(), L'\xFEFF');

    if (FILE* out = _wfopen(path.c_str(), L"wb")) {
        fwrite(wide.data(), sizeof(wchar_t), wide.size(), out);
        fclose(out);
        logx::Info("Converted %s to UTF-16", text::Narrow(path).c_str());
    }
}

std::string SanitizePrefix(std::string prefix) {
    prefix = text::Trim(prefix, " \t");
    if (prefix.empty() || prefix.size() > 8) return ".";
    return prefix;
}

}  // namespace

void Init(const std::wstring& dataDirectory) {
    g_dir = dataDirectory;
    g_path = dataDirectory + L"\\config.ini";
    if (GetFileAttributesW(g_path.c_str()) == INVALID_FILE_ATTRIBUTES) WriteDefaultFile();
    Reload();
}

void Reload() {
    EnsureUtf16(g_path);
    Config& c = g_config;
    const Config d;  // defaults

    c.unloadKey = ReadKey("General", "UnloadKey", d.unloadKey);
    c.requireHiddenCursor = ReadBool("General", "RequireHiddenCursor", d.requireHiddenCursor);
    c.menuKey = ReadKey("Menu", "Key", d.menuKey);

    c.chatCommands = ReadBool("Chat", "Commands", d.chatCommands);
    c.chatOpenKey = ReadKey("Chat", "OpenKey", d.chatOpenKey);
    c.chatCommandKey = ReadKey("Chat", "CommandKey", d.chatCommandKey);
    const std::string prefix = SanitizePrefix(ReadString("Chat", "Prefix", "."));
    AcquireSRWLockExclusive(&g_prefixLock);
    g_prefix = prefix;
    ReleaseSRWLockExclusive(&g_prefixLock);

    c.sprintEnabled = ReadBool("AutoSprint", "Enabled", d.sprintEnabled);
    c.sprintToggleKey = ReadKey("AutoSprint", "ToggleKey", d.sprintToggleKey);
    c.forwardKey = ReadKey("AutoSprint", "ForwardKey", d.forwardKey);
    c.sprintKey = ReadKey("AutoSprint", "SprintKey", d.sprintKey);
    c.sprintFallbackSendInput = ReadBool("AutoSprint", "FallbackSendInput", d.sprintFallbackSendInput);

    c.zoomEnabled = ReadBool("Zoom", "Enabled", d.zoomEnabled);
    c.zoomKey = ReadKey("Zoom", "Key", d.zoomKey);
    c.zoomToggle = ReadBool("Zoom", "Toggle", d.zoomToggle);
    float factor = ReadFloat("Zoom", "Factor", d.zoomFactor);
    float minFactor = ReadFloat("Zoom", "MinFactor", d.zoomMinFactor);
    float maxFactor = ReadFloat("Zoom", "MaxFactor", d.zoomMaxFactor);
    c.zoomScrollAdjust = ReadBool("Zoom", "ScrollAdjust", d.zoomScrollAdjust);
    float scrollStep = ReadFloat("Zoom", "ScrollStep", d.zoomScrollStep);
    c.zoomRememberScroll = ReadBool("Zoom", "RememberScroll", d.zoomRememberScroll);
    bool smooth = ReadBool("Zoom", "Smooth", d.zoomSmooth);
    const float smoothSpeed = ReadFloat("Zoom", "SmoothSpeed", d.zoomSmoothSpeed);
    c.zoomHand = ReadBool("Zoom", "ZoomHand", d.zoomHand);

    // Keep the numbers sane so a typo cannot produce a 0 or negative FOV.
    if (minFactor < 1.0f) minFactor = 1.0f;
    if (maxFactor < minFactor) maxFactor = minFactor;
    if (factor < minFactor) factor = minFactor;
    if (factor > maxFactor) factor = maxFactor;
    if (scrollStep <= 1.0f) scrollStep = 1.25f;
    if (smoothSpeed <= 0.0f) smooth = false;
    c.zoomFactor = factor;
    c.zoomMinFactor = minFactor;
    c.zoomMaxFactor = maxFactor;
    c.zoomScrollStep = scrollStep;
    c.zoomSmooth = smooth;
    c.zoomSmoothSpeed = smoothSpeed;

    c.fullbrightEnabled = ReadBool("Fullbright", "Enabled", d.fullbrightEnabled);
    c.fullbrightToggleKey = ReadKey("Fullbright", "ToggleKey", d.fullbrightToggleKey);
    c.fullbrightGamma = ReadFloat("Fullbright", "Gamma", d.fullbrightGamma);

    c.textHotkeyEnabled = ReadBool("TextHotkey", "Enabled", d.textHotkeyEnabled);
    c.textHotkeyToggleKey = ReadKey("TextHotkey", "ToggleKey", d.textHotkeyToggleKey);
    float cooldown = ReadFloat("TextHotkey", "Cooldown", d.textHotkeyCooldown);
    c.textHotkeyCooldown = cooldown < 0.0f ? 0.0f : cooldown;
    c.textHotkeys = ReadTextHotkeys();

    c.sigGetFov = ReadString("Signatures", "GetFov", "");
    c.sigKeyboardFeed = ReadString("Signatures", "KeyboardFeed", "");
    c.sigMouseFeed = ReadString("Signatures", "MouseFeed", "");
    c.sigGetGamma = ReadString("Signatures", "GetGamma", "");

    logx::Info("Config: prefix '%s', AutoSprint=%d [%s], Zoom=%d [%s, %s, x%.2f], Fullbright=%d [%s], "
               "TextHotkeys=%zu, menu [%s], unload [%s]",
               prefix.c_str(), c.sprintEnabled.load(), keys::Name(c.sprintToggleKey).c_str(), c.zoomEnabled.load(),
               keys::Name(c.zoomKey).c_str(), c.zoomToggle ? "toggle" : "hold", c.zoomFactor.load(),
               c.fullbrightEnabled.load(), keys::Name(c.fullbrightToggleKey).c_str(), c.textHotkeys.size(),
               keys::Name(c.menuKey).c_str(), keys::Name(c.unloadKey).c_str());

    // Menu appearance ([Menu] + [Theme]) and the data the menu shows.
    gui::OnConfigReloaded();
}

bool Set(const std::string& section, const std::string& key, const std::string& value) {
    const bool ok = WritePrivateProfileStringW(text::Widen(section).c_str(), text::Widen(key).c_str(),
                                               text::Widen(value).c_str(), g_path.c_str()) != FALSE;
    if (!ok) logx::Error("Could not write [%s] %s to config.ini (error %lu)", section.c_str(), key.c_str(), GetLastError());
    Reload();
    return ok;
}

bool Remove(const std::string& section, const std::string& key) {
    const bool ok = WritePrivateProfileStringW(text::Widen(section).c_str(), text::Widen(key).c_str(), nullptr,
                                               g_path.c_str()) != FALSE;
    Reload();
    return ok;
}

bool Apply(const std::vector<ConfigEntry>& entries) {
    bool ok = true;
    for (const ConfigEntry& e : entries) {
        const std::wstring section = text::Widen(e.section);
        const std::wstring key = text::Widen(e.key);
        BOOL written = FALSE;
        switch (e.op) {
            case ConfigEntry::Op::Set:
                written = WritePrivateProfileStringW(section.c_str(), key.c_str(), text::Widen(e.value).c_str(),
                                                     g_path.c_str());
                break;
            case ConfigEntry::Op::Remove:
                written = WritePrivateProfileStringW(section.c_str(), key.c_str(), nullptr, g_path.c_str());
                break;
            case ConfigEntry::Op::RemoveSection:
                written = WritePrivateProfileStringW(section.c_str(), nullptr, nullptr, g_path.c_str());
                break;
        }
        if (!written) {
            logx::Error("Could not write [%s] %s to config.ini (error %lu)", e.section.c_str(), e.key.c_str(),
                        GetLastError());
            ok = false;
        }
    }
    Reload();
    return ok;
}

std::vector<std::pair<std::string, std::string>> ReadSection(const std::string& section) {
    std::vector<std::pair<std::string, std::string>> out;
    std::vector<wchar_t> buffer(65536);
    const DWORD len = GetPrivateProfileSectionW(text::Widen(section).c_str(), buffer.data(),
                                                static_cast<DWORD>(buffer.size()), g_path.c_str());
    for (const wchar_t* p = buffer.data(); p < buffer.data() + len && *p; p += wcslen(p) + 1) {
        const std::string line = text::Narrow(p);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = line.substr(eq + 1);
        const size_t comment = value.find(" ;");
        if (comment != std::string::npos) value.erase(comment);
        out.emplace_back(text::Trim(line.substr(0, eq), " \t"), text::Trim(value, " \t\""));
    }
    return out;
}

std::string Get(const std::string& section, const std::string& key) { return ReadRaw(section, key, ""); }

std::string Prefix() {
    AcquireSRWLockShared(&g_prefixLock);
    std::string copy = g_prefix;
    ReleaseSRWLockShared(&g_prefixLock);
    return copy;
}

const std::wstring& Directory() { return g_dir; }
const std::wstring& Path() { return g_path; }

}  // namespace config
