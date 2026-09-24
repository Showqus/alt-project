#include "common.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace launcher {

Settings g_settings;

namespace {

CRITICAL_SECTION g_logLock;
bool g_logReady = false;
bool g_console = false;

const char kDefaultIni[] =
    "; BedrockQoL launcher settings\n"
    "\n"
    "[Launcher]\n"
    "; Inject automatically whenever Minecraft is started.\n"
    "AutoInject=1\n"
    "; Seconds to wait after the game process starts before injecting.\n"
    "InjectDelaySeconds=5\n"
    "; 1 = tray icon with menu and notifications, 0 = completely invisible.\n"
    "Tray=1\n"
    "; Show messages from the mod (command results, toggles) as Windows notifications.\n"
    "Notifications=1\n"
    "\n"
    "[Update]\n"
    "; Download new builds of the DLL from GitHub and reload them into the running game.\n"
    "Enabled=1\n"
    "ManifestUrl=https://github.com/Showqus/alt-project/releases/download/latest/manifest.txt\n"
    "CheckIntervalMinutes=10\n"
    "\n"
    "[Game]\n"
    "Process=Minecraft.Windows.exe\n"
    "; Empty = %LOCALAPPDATA%\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\RoamingState\\BedrockQoL\n"
    "DataDir=\n";

std::wstring IniPath() { return ExeDirectory() + L"\\launcher.ini"; }

std::wstring ReadIni(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buffer[2048];
    GetPrivateProfileStringW(section, key, def, buffer, 2048, IniPath().c_str());
    std::wstring value = buffer;
    const size_t comment = value.find(L" ;");
    if (comment != std::wstring::npos) value.erase(comment);
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) value.pop_back();
    return value;
}

bool ReadBool(const wchar_t* section, const wchar_t* key, bool def) {
    const std::wstring v = ReadIni(section, key, def ? L"1" : L"0");
    return !v.empty() && (v[0] == L'1' || v[0] == L't' || v[0] == L'T' || v[0] == L'y' || v[0] == L'Y');
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def) {
    const std::wstring v = ReadIni(section, key, L"");
    return v.empty() ? def : _wtoi(v.c_str());
}

}  // namespace

const std::wstring& ExeDirectory() {
    static const std::wstring dir = [] {
        wchar_t path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path, len);
        const size_t slash = p.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring(L".") : p.substr(0, slash);
    }();
    return dir;
}

std::wstring DllDirectory() {
    const std::wstring dir = ExeDirectory() + L"\\dll";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring GameDataDirectory() {
    if (!g_settings.gameDataDir.empty()) return g_settings.gameDataDir;
    PWSTR localAppData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        dir = std::wstring(localAppData) +
              L"\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\RoamingState\\BedrockQoL";
        CoTaskMemFree(localAppData);
    }
    return dir;
}

void LoadSettings() {
    if (!FileExists(IniPath())) WriteFileUtf8(IniPath(), kDefaultIni);

    Settings& s = g_settings;
    const Settings d;
    s.autoInject = ReadBool(L"Launcher", L"AutoInject", d.autoInject);
    s.injectDelaySeconds = ReadInt(L"Launcher", L"InjectDelaySeconds", d.injectDelaySeconds);
    s.tray = ReadBool(L"Launcher", L"Tray", d.tray);
    s.notifications = ReadBool(L"Launcher", L"Notifications", d.notifications);
    s.updateEnabled = ReadBool(L"Update", L"Enabled", d.updateEnabled);
    s.manifestUrl = ReadIni(L"Update", L"ManifestUrl", d.manifestUrl.c_str());
    s.checkIntervalMinutes = ReadInt(L"Update", L"CheckIntervalMinutes", d.checkIntervalMinutes);
    s.processName = ReadIni(L"Game", L"Process", d.processName.c_str());
    s.gameDataDir = ReadIni(L"Game", L"DataDir", L"");

    if (s.injectDelaySeconds < 0) s.injectDelaySeconds = 0;
    if (s.checkIntervalMinutes < 1) s.checkIntervalMinutes = 1;
}

void EnableConsoleLog() { g_console = true; }

void Log(const wchar_t* fmt, ...) {
    wchar_t message[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(message, 2047, fmt, args);
    message[2047] = L'\0';
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[2200];
    _snwprintf(line, 2199, L"[%04u-%02u-%02u %02u:%02u:%02u] %ls\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond, message);
    line[2199] = L'\0';

    if (!g_logReady) {
        InitializeCriticalSection(&g_logLock);
        g_logReady = true;
    }
    EnterCriticalSection(&g_logLock);
    WriteFileUtf8(ExeDirectory() + L"\\launcher.log", Narrow(line), true);
    if (g_console) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        if (!WriteConsoleW(out, line, static_cast<DWORD>(wcslen(line)), &written, nullptr)) {
            const std::string utf8 = Narrow(line);
            WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
    LeaveCriticalSection(&g_logLock);
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], len);
    return out;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], len, nullptr, nullptr);
    return out;
}

bool FileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    out.clear();
    char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& data, bool append) {
    FILE* f = _wfopen(path.c_str(), append ? L"ab" : L"wb");
    if (!f) return false;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

bool SendGameCommand(const std::string& command) {
    const std::wstring control = GameDataDirectory() + L"\\control";
    CreateDirectoryW(GameDataDirectory().c_str(), nullptr);
    CreateDirectoryW(control.c_str(), nullptr);
    const bool ok = WriteFileUtf8(control + L"\\commands.txt", command + "\n", true);
    Log(L"Command for the game: %ls (%ls)", Widen(command).c_str(), ok ? L"queued" : L"FAILED");
    return ok;
}

}  // namespace launcher
