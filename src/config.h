#pragma once

#include <atomic>
#include <string>
#include <vector>

struct TextHotkeyEntry {
    std::string id;    // INI key in [TextHotkeys] ("1", "2", ...)
    int key = 0;       // virtual-key code
    std::string text;  // UTF-8 text to send; if it starts with the command prefix it runs as a command
};

// One change to config.ini, used for batched writes from the menu.
struct ConfigEntry {
    enum class Op { Set, Remove, RemoveSection };
    Op op = Op::Set;
    std::string section;
    std::string key;
    std::string value;
};

// Scalars are atomic because they are read from game threads (input/render hooks) while the
// worker thread may reload or change them.
struct Config {
    // [General]
    std::atomic<int> unloadKey{0x23};  // END
    std::atomic<bool> requireHiddenCursor{true};

    // [Menu] (the appearance keys of [Menu] and [Theme] are parsed by gui::theme)
    std::atomic<bool> menuEnabled{true};
    std::atomic<int> menuKey{0x2D};  // INSERT
    std::atomic<int> menuStartDelay{20};  // seconds after the game started before Direct3D is touched

    // [Chat]
    std::atomic<bool> chatCommands{true};
    std::atomic<int> chatOpenKey{'T'};
    std::atomic<int> chatCommandKey{0xBF};  // '/' key (VK_OEM_2)

    // [AutoSprint]
    std::atomic<bool> sprintEnabled{true};
    std::atomic<int> sprintToggleKey{0x77};  // F8
    std::atomic<int> forwardKey{'W'};
    std::atomic<int> sprintKey{0x11};  // CTRL
    std::atomic<bool> sprintFallbackSendInput{true};

    // [Zoom]
    std::atomic<bool> zoomEnabled{true};
    std::atomic<int> zoomKey{'C'};
    std::atomic<bool> zoomToggle{false};
    std::atomic<float> zoomFactor{4.0f};
    std::atomic<float> zoomMinFactor{1.5f};
    std::atomic<float> zoomMaxFactor{50.0f};
    std::atomic<bool> zoomScrollAdjust{true};
    std::atomic<float> zoomScrollStep{1.25f};
    std::atomic<bool> zoomRememberScroll{false};
    std::atomic<bool> zoomSmooth{true};
    std::atomic<float> zoomSmoothSpeed{12.0f};
    std::atomic<bool> zoomHand{false};

    // [Fullbright]
    std::atomic<bool> fullbrightEnabled{false};
    std::atomic<int> fullbrightToggleKey{0};
    std::atomic<float> fullbrightGamma{25.0f};

    // [TextHotkey] + [TextHotkeys]
    std::atomic<bool> textHotkeyEnabled{true};
    std::atomic<int> textHotkeyToggleKey{0};
    std::atomic<float> textHotkeyCooldown{1.0f};
    std::vector<TextHotkeyEntry> textHotkeys;  // worker thread only (the menu reads gui::Snapshot)

    // [Signatures] - empty means "use the built-in ones". Read once at startup.
    std::string sigGetFov;
    std::string sigKeyboardFeed;
    std::string sigMouseFeed;
    std::string sigGetGamma;
};

// Global configuration.
extern Config g_config;

namespace config {

// Remembers the data directory, writes a commented default config.ini if missing, then loads it.
void Init(const std::wstring& dataDirectory);

// Re-reads config.ini (after it was edited, or a profile was loaded).
void Reload();

// Writes one value into config.ini and reloads. Returns false if the file could not be written.
bool Set(const std::string& section, const std::string& key, const std::string& value);

// Removes one key from config.ini and reloads.
bool Remove(const std::string& section, const std::string& key);

// Applies several changes, then reloads once.
bool Apply(const std::vector<ConfigEntry>& entries);

// All "key=value" lines of a section (comments skipped, values trimmed, inline comments removed).
std::vector<std::pair<std::string, std::string>> ReadSection(const std::string& section);

// Raw value from config.ini ("" if missing).
std::string Get(const std::string& section, const std::string& key);

// Chat command prefix (e.g. "." or ";"). Thread-safe copy.
std::string Prefix();

const std::wstring& Directory();  // ...\RoamingState\BedrockQoL
const std::wstring& Path();       // ...\config.ini

}  // namespace config
