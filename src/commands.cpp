#include "commands.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.h"
#include "features/texthotkey.h"
#include "hooks.h"
#include "keys.h"
#include "log.h"
#include "notify.h"
#include "text.h"

#ifndef BEDROCKQOL_VERSION
#define BEDROCKQOL_VERSION "dev"
#endif

namespace commands {
namespace {

// Everything a key can be bound to.
struct Function {
    const char* name;
    std::vector<const char*> aliases;
    const char* section;       // INI section
    const char* keyEntry;      // INI key that stores the bound key
    const char* enabledEntry;  // INI key with the on/off state, or nullptr
    const char* title;         // shown in messages
    std::atomic<int> Config::*key;
    std::atomic<bool> Config::*enabled;
};

const std::vector<Function> kFunctions = {
    {"autosprint", {"sprint", "as", "бег", "спринт"}, "AutoSprint", "ToggleKey", "Enabled", "AutoSprint",
     &Config::sprintToggleKey, &Config::sprintEnabled},
    {"zoom", {"z", "зум"}, "Zoom", "Key", "Enabled", "Zoom", &Config::zoomKey, &Config::zoomEnabled},
    {"fullbright", {"fb", "bright", "яркость"}, "Fullbright", "ToggleKey", "Enabled", "Fullbright",
     &Config::fullbrightToggleKey, &Config::fullbrightEnabled},
    {"texthotkey", {"th", "texthotkeys"}, "TextHotkey", "ToggleKey", "Enabled", "TextHotkey",
     &Config::textHotkeyToggleKey, &Config::textHotkeyEnabled},
    {"unload", {"eject", "выгрузить"}, "General", "UnloadKey", nullptr, "Выгрузка мода", &Config::unloadKey, nullptr},
};

std::string LowerUtf8(const std::string& s) {
    std::wstring w = text::Widen(s);
    if (!w.empty()) CharLowerBuffW(&w[0], static_cast<DWORD>(w.size()));
    return text::Narrow(w);
}

const Function* FindFunction(const std::string& name) {
    const std::string n = LowerUtf8(name);
    for (const Function& f : kFunctions) {
        if (n == f.name) return &f;
        for (const char* alias : f.aliases) {
            if (n == alias) return &f;
        }
    }
    return nullptr;
}

std::string FunctionList() {
    std::string out;
    for (const Function& f : kFunctions) {
        if (!out.empty()) out += ", ";
        out += f.name;
    }
    return out;
}

// Key name from chat: "K", "F6", "CTRL", or a letter typed on another layout ("л" = the K key).
int ParseKey(const std::string& token) {
    const int vk = keys::Parse(token);
    if (vk >= 0) return vk;

    const std::wstring w = text::Widen(token);
    if (w.size() == 1) {
        HKL layouts[16];
        const int count = GetKeyboardLayoutList(16, layouts);
        for (int i = 0; i < count; ++i) {
            const SHORT scan = VkKeyScanExW(w[0], layouts[i]);
            if (scan != -1 && (scan & 0xFF) != 0xFF) return scan & 0xFF;
        }
    }
    return -1;
}

int CurrentKey(const Function& f) { return (g_config.*f.key).load(); }

bool IsEnabled(const Function& f) { return f.enabled ? (g_config.*f.enabled).load() : true; }

std::string OnOff(bool on) { return on ? "ВКЛ" : "ВЫКЛ"; }

void SetEnabled(const Function& f, bool on) {
    config::Set(f.section, f.enabledEntry, on ? "1" : "0");
    notify::Send(std::string(f.title) + ": " + OnOff(on));
}

bool ValidProfileName(const std::string& name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

std::wstring SubDir(const wchar_t* name) {
    const std::wstring dir = config::Directory() + L"\\" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ProfilePath(const wchar_t* folder, const std::string& name) {
    return SubDir(folder) + L"\\" + text::Widen(name) + L".ini";
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string ListIni(const wchar_t* folder) {
    std::string out;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((SubDir(folder) + L"\\*.ini").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = data.cFileName;
        name.resize(name.size() - 4);
        if (!out.empty()) out += ", ";
        out += text::Narrow(name);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return out;
}

std::string Join(const std::vector<std::string>& args, size_t from) {
    std::string out;
    for (size_t i = from; i < args.size(); ++i) {
        if (!out.empty()) out += ' ';
        out += args[i];
    }
    return out;
}

// --- Commands ----------------------------------------------------------------------------

void Help() {
    const std::string p = config::Prefix();
    notify::Send("Команды BedrockQoL (префикс " + p + "):\n" +
                 p + "bind <клавиша> <функция>, " + p + "unbind <функция|клавиша>, " + p + "binds\n" +
                 p + "toggle <функция>\n" +
                 p + "prefix <новый префикс>\n" +
                 p + "th add <клавиша> <текст>, " + p + "th remove <номер|клавиша>, " + p + "th list\n" +
                 p + "config save|load|delete|export|import <имя>, " + p + "config list\n" +
                 p + "set <Секция.Ключ> <значение>, " + p + "get <Секция.Ключ>\n" +
                 p + "unload, " + p + "version\n" +
                 "Функции: " + FunctionList());
}

void Bind(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        notify::Send("Использование: " + config::Prefix() + "bind <клавиша> <функция>. Функции: " + FunctionList());
        return;
    }
    // Accept both ".bind K zoom" and ".bind zoom K".
    const Function* f = FindFunction(args[2]);
    std::string keyToken = args[1];
    if (!f) {
        f = FindFunction(args[1]);
        keyToken = args[2];
    }
    if (!f) {
        notify::Send("Неизвестная функция '" + args[2] + "'. Доступно: " + FunctionList());
        return;
    }
    const int vk = ParseKey(keyToken);
    if (vk <= 0) {
        notify::Send("Неизвестная клавиша '" + keyToken + "'");
        return;
    }

    std::string warning;
    for (const Function& other : kFunctions) {
        if (&other != f && CurrentKey(other) == vk) warning += std::string(" (эта клавиша также у ") + other.name + ")";
    }
    config::Set(f->section, f->keyEntry, keys::Name(vk));
    notify::Send(std::string(f->title) + " -> " + keys::Name(vk) + warning);
}

void Unbind(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        notify::Send("Использование: " + config::Prefix() + "unbind <функция|клавиша>");
        return;
    }
    if (const Function* f = FindFunction(args[1])) {
        config::Set(f->section, f->keyEntry, "NONE");
        notify::Send(std::string(f->title) + ": клавиша снята");
        return;
    }
    const int vk = ParseKey(args[1]);
    bool any = false;
    for (const Function& f : kFunctions) {
        if (vk > 0 && CurrentKey(f) == vk) {
            config::Set(f.section, f.keyEntry, "NONE");
            notify::Send(std::string(f.title) + ": клавиша снята");
            any = true;
        }
    }
    if (!any) notify::Send("Нет привязок для '" + args[1] + "'");
}

void Binds() {
    std::string out = "Привязки:";
    for (const Function& f : kFunctions) {
        out += "\n" + std::string(f.name) + " = " + keys::Name(CurrentKey(f));
        if (f.enabledEntry) out += " [" + OnOff(IsEnabled(f)) + "]";
    }
    for (const TextHotkeyEntry& e : g_config.textHotkeys) {
        out += "\nth #" + e.id + " " + keys::Name(e.key) + " = " + e.text;
    }
    notify::Send(out);
}

void Toggle(const std::vector<std::string>& args) {
    const Function* f = args.size() >= 2 ? FindFunction(args[1]) : nullptr;
    if (!f || !f->enabledEntry) {
        notify::Send("Использование: " + config::Prefix() + "toggle <autosprint|zoom|fullbright|texthotkey>");
        return;
    }
    SetEnabled(*f, !IsEnabled(*f));
}

void Prefix(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        notify::Send("Текущий префикс: " + config::Prefix() + ". Сменить: " + config::Prefix() + "prefix <символы>");
        return;
    }
    const std::string prefix = args[1];
    if (prefix.size() > 8 || prefix.find_first_of("/ \t") != std::string::npos) {
        notify::Send("Префикс: 1-8 символов, без пробелов и '/'");
        return;
    }
    config::Set("Chat", "Prefix", prefix);
    notify::Send("Новый префикс команд: " + prefix + " (например " + prefix + "bind K zoom)");
}

void TextHotkeyCmd(const std::vector<std::string>& args) {
    const std::string p = config::Prefix();
    const std::string sub = args.size() >= 2 ? text::Lower(args[1]) : "list";

    if (sub == "add" && args.size() >= 4) {
        const int vk = ParseKey(args[2]);
        if (vk <= 0) {
            notify::Send("Неизвестная клавиша '" + args[2] + "'");
            return;
        }
        int next = 1;
        for (const TextHotkeyEntry& e : g_config.textHotkeys) next = (std::max)(next, std::atoi(e.id.c_str()) + 1);
        const std::string message = Join(args, 3);
        config::Set("TextHotkeys", std::to_string(next), keys::Name(vk) + "|" + message);
        notify::Send("TextHotkey #" + std::to_string(next) + ": " + keys::Name(vk) + " -> " + message);
        return;
    }
    if ((sub == "remove" || sub == "del" || sub == "delete") && args.size() >= 3) {
        const int vk = ParseKey(args[2]);
        bool any = false;
        const std::vector<TextHotkeyEntry> entries = g_config.textHotkeys;  // Remove() reloads the list
        for (const TextHotkeyEntry& e : entries) {
            if (e.id == args[2] || (vk > 0 && e.key == vk)) {
                config::Remove("TextHotkeys", e.id);
                notify::Send("TextHotkey #" + e.id + " удалён");
                any = true;
            }
        }
        if (!any) notify::Send("TextHotkey '" + args[2] + "' не найден");
        return;
    }
    if (sub == "clear") {
        const std::vector<TextHotkeyEntry> entries = g_config.textHotkeys;
        for (const TextHotkeyEntry& e : entries) config::Remove("TextHotkeys", e.id);
        notify::Send("Все TextHotkey удалены");
        return;
    }
    if (sub == "list") {
        std::string out = "TextHotkey [" + OnOff(g_config.textHotkeyEnabled) + "]:";
        for (const TextHotkeyEntry& e : g_config.textHotkeys) out += "\n#" + e.id + " " + keys::Name(e.key) + " = " + e.text;
        if (g_config.textHotkeys.empty()) out += " пусто. Добавить: " + p + "th add F6 gg";
        notify::Send(out);
        return;
    }
    notify::Send("Использование: " + p + "th add <клавиша> <текст> | " + p + "th remove <номер|клавиша> | " + p +
                 "th list | " + p + "th clear");
}

void ConfigCmd(const std::vector<std::string>& args) {
    const std::string p = config::Prefix();
    const std::string sub = args.size() >= 2 ? text::Lower(args[1]) : "";
    const std::string name = args.size() >= 3 ? Join(args, 2) : "";

    if (sub == "list") {
        const std::string list = ListIni(L"configs");
        notify::Send("Конфиги: " + (list.empty() ? std::string("нет") : list) +
                     "\nДля импорта: " + ListIni(L"imports"));
        return;
    }
    if (sub == "reload") {
        config::Reload();
        notify::Send("config.ini перечитан");
        return;
    }
    if (sub == "path") {
        notify::Send(text::Narrow(config::Directory()));
        return;
    }
    if (sub.empty() || name.empty()) {
        notify::Send("Использование: " + p + "config save|load|delete|export|import <имя>, " + p + "config list");
        return;
    }
    if (!ValidProfileName(name)) {
        notify::Send("Недопустимое имя конфига: " + name);
        return;
    }

    const std::wstring profile = ProfilePath(L"configs", name);
    if (sub == "save") {
        const bool ok = CopyFileW(config::Path().c_str(), profile.c_str(), FALSE) != FALSE;
        notify::Send(ok ? "Конфиг '" + name + "' сохранён" : "Не удалось сохранить конфиг '" + name + "'");
    } else if (sub == "load") {
        if (!Exists(profile)) {
            notify::Send("Конфиг '" + name + "' не найден. Есть: " + ListIni(L"configs"));
            return;
        }
        const bool ok = CopyFileW(profile.c_str(), config::Path().c_str(), FALSE) != FALSE;
        config::Reload();
        notify::Send(ok ? "Конфиг '" + name + "' загружен" : "Не удалось загрузить конфиг '" + name + "'");
    } else if (sub == "delete" || sub == "remove") {
        const bool ok = DeleteFileW(profile.c_str()) != FALSE;
        notify::Send(ok ? "Конфиг '" + name + "' удалён" : "Конфиг '" + name + "' не найден");
    } else if (sub == "export") {
        const std::wstring source = Exists(profile) ? profile : config::Path();
        const std::wstring target = ProfilePath(L"exports", name);
        const bool ok = CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE;
        notify::Send(ok ? "Экспортирован в " + text::Narrow(target) : "Не удалось экспортировать '" + name + "'");
    } else if (sub == "import") {
        const std::wstring source = ProfilePath(L"imports", name);
        if (!Exists(source)) {
            notify::Send("Положите файл " + name + ".ini в папку " + text::Narrow(SubDir(L"imports")) +
                         " (или используйте Import в меню лаунчера)");
            return;
        }
        const bool ok = CopyFileW(source.c_str(), profile.c_str(), FALSE) != FALSE &&
                        CopyFileW(source.c_str(), config::Path().c_str(), FALSE) != FALSE;
        config::Reload();
        notify::Send(ok ? "Конфиг '" + name + "' импортирован и загружен" : "Не удалось импортировать '" + name + "'");
    } else {
        notify::Send("Использование: " + p + "config save|load|delete|export|import <имя>, " + p + "config list");
    }
}

bool SplitSettingName(const std::string& token, std::string& section, std::string& key) {
    const size_t dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) return false;
    section = token.substr(0, dot);
    key = token.substr(dot + 1);
    return true;
}

void SetCmd(const std::vector<std::string>& args) {
    std::string section, key;
    if (args.size() < 3 || !SplitSettingName(args[1], section, key)) {
        notify::Send("Использование: " + config::Prefix() + "set <Секция.Ключ> <значение>, например " +
                     config::Prefix() + "set Zoom.Factor 6");
        return;
    }
    const std::string value = Join(args, 2);
    config::Set(section, key, value);
    notify::Send(section + "." + key + " = " + value);
}

void GetCmd(const std::vector<std::string>& args) {
    std::string section, key;
    if (args.size() < 2 || !SplitSettingName(args[1], section, key)) {
        notify::Send("Использование: " + config::Prefix() + "get <Секция.Ключ>");
        return;
    }
    notify::Send(section + "." + key + " = " + config::Get(section, key));
}

}  // namespace

void Execute(const std::string& line) {
    const std::vector<std::string> args = text::Split(line);
    if (args.empty()) return;
    const std::string cmd = LowerUtf8(args[0]);
    logx::Info("Command: %s", line.c_str());

    if (cmd == "help" || cmd == "?" || cmd == "помощь") {
        Help();
    } else if (cmd == "bind" || cmd == "b") {
        Bind(args);
    } else if (cmd == "unbind" || cmd == "ub") {
        Unbind(args);
    } else if (cmd == "binds" || cmd == "keybinds") {
        Binds();
    } else if (cmd == "toggle" || cmd == "t") {
        Toggle(args);
    } else if (cmd == "prefix") {
        Prefix(args);
    } else if (cmd == "th" || cmd == "texthotkey") {
        TextHotkeyCmd(args);
    } else if (cmd == "config" || cmd == "cfg") {
        ConfigCmd(args);
    } else if (cmd == "set") {
        SetCmd(args);
    } else if (cmd == "get") {
        GetCmd(args);
    } else if (cmd == "say") {
        texthotkey::SendChatMessage(Join(args, 1));
    } else if (cmd == "unload" || cmd == "eject") {
        notify::Send("BedrockQoL выгружается");
        hooks::RequestUnload();
    } else if (cmd == "version" || cmd == "ver") {
        notify::Send(std::string("BedrockQoL ") + BEDROCKQOL_VERSION);
    } else if (const Function* f = FindFunction(cmd); f && f->enabledEntry) {
        // Shortcut: ".zoom" == ".toggle zoom"
        SetEnabled(*f, !IsEnabled(*f));
    } else {
        notify::Send("Неизвестная команда '" + args[0] + "'. Список: " + config::Prefix() + "help");
    }
}

bool OnKeyPress(int vk) {
    if (vk <= 0) return false;
    bool used = false;
    for (const Function& f : kFunctions) {
        // Zoom (hold key) and unload are handled directly on the input thread.
        if (!f.enabledEntry || std::string(f.name) == "zoom") continue;
        if (CurrentKey(f) == vk) {
            SetEnabled(f, !IsEnabled(f));
            used = true;
        }
    }
    if (texthotkey::OnKeyPress(vk)) used = true;
    return used;
}

}  // namespace commands
