#include "keys.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace keys {
namespace {

struct NamedKey {
    const char* name;
    int vk;
};

// Minecraft Bedrock (UWP) reports the generic modifier codes (VK_CONTROL, VK_SHIFT, VK_MENU),
// so CTRL/SHIFT/ALT map to those rather than to the left/right variants.
const NamedKey kNamedKeys[] = {
    {"NONE", 0},          {"CTRL", VK_CONTROL},  {"CONTROL", VK_CONTROL}, {"SHIFT", VK_SHIFT},
    {"ALT", VK_MENU},     {"SPACE", VK_SPACE},   {"TAB", VK_TAB},         {"CAPSLOCK", VK_CAPITAL},
    {"ESC", VK_ESCAPE},   {"ENTER", VK_RETURN},  {"BACKSPACE", VK_BACK},  {"END", VK_END},
    {"HOME", VK_HOME},    {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},   {"PAGEUP", VK_PRIOR},
    {"PAGEDOWN", VK_NEXT}, {"UP", VK_UP},         {"DOWN", VK_DOWN},       {"LEFT", VK_LEFT},
    {"RIGHT", VK_RIGHT},  {"NUMPAD0", VK_NUMPAD0}, {"NUMPAD1", VK_NUMPAD1}, {"NUMPAD2", VK_NUMPAD2},
    {"NUMPAD3", VK_NUMPAD3}, {"NUMPAD4", VK_NUMPAD4}, {"NUMPAD5", VK_NUMPAD5}, {"NUMPAD6", VK_NUMPAD6},
    {"NUMPAD7", VK_NUMPAD7}, {"NUMPAD8", VK_NUMPAD8}, {"NUMPAD9", VK_NUMPAD9}, {"GRAVE", VK_OEM_3},
    {"MINUS", VK_OEM_MINUS}, {"EQUALS", VK_OEM_PLUS}, {"LBRACKET", VK_OEM_4}, {"RBRACKET", VK_OEM_6},
    {"SEMICOLON", VK_OEM_1}, {"APOSTROPHE", VK_OEM_7}, {"COMMA", VK_OEM_COMMA}, {"PERIOD", VK_OEM_PERIOD},
    {"SLASH", VK_OEM_2},  {"BACKSLASH", VK_OEM_5}, {"LSHIFT", VK_LSHIFT}, {"RSHIFT", VK_RSHIFT},
    {"LCTRL", VK_LCONTROL}, {"RCTRL", VK_RCONTROL}, {"LALT", VK_LMENU}, {"RALT", VK_RMENU},
    {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2}, {"MMB", VK_MBUTTON},
};

std::string Normalize(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

int Parse(const std::string& raw) {
    const std::string name = Normalize(raw);
    if (name.empty()) return 0;

    if (name.size() == 1) {
        const char c = name[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }

    if (name[0] == 'F' && name.size() <= 3) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }

    for (const auto& k : kNamedKeys) {
        if (name == k.name) return k.vk;
    }

    char* end = nullptr;
    const long value = std::strtol(name.c_str(), &end, 0);
    if (end && *end == '\0' && value > 0 && value < 256) return static_cast<int>(value);

    return -1;
}

std::string Name(int vk) {
    if (vk == 0) return "NONE";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const auto& k : kNamedKeys) {
        if (k.vk == vk) return k.name;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

}  // namespace keys
