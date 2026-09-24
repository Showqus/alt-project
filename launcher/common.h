#pragma once

#include <string>

// Shared launcher helpers: paths, settings (launcher.ini), logging.
namespace launcher {

struct Settings {
    bool autoInject = true;
    int injectDelaySeconds = 5;
    bool tray = true;
    bool notifications = true;

    bool updateEnabled = true;
    std::wstring manifestUrl = L"https://github.com/Showqus/alt-project/releases/download/latest/manifest.txt";
    int checkIntervalMinutes = 10;

    std::wstring processName = L"Minecraft.Windows.exe";
    std::wstring gameDataDir;  // empty = the UWP package's RoamingState\BedrockQoL
};

extern Settings g_settings;

// Folder of BedrockQoLLauncher.exe.
const std::wstring& ExeDirectory();

// Where downloaded DLLs are kept (<exe>\dll).
std::wstring DllDirectory();

// The mod's data folder inside the game package (config.ini, control\...).
std::wstring GameDataDirectory();

// Loads launcher.ini (creates it with defaults if missing).
void LoadSettings();

// Log to <exe>\launcher.log (and the console, if one is attached).
void Log(const wchar_t* fmt, ...);
void EnableConsoleLog();

// UTF-8 / UTF-16 helpers.
std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

bool FileExists(const std::wstring& path);
bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8(const std::wstring& path, const std::string& data, bool append = false);

// Appends a command for the DLL (control\commands.txt), e.g. "unload" or "config import pvp".
bool SendGameCommand(const std::string& command);

}  // namespace launcher
