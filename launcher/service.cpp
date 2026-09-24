#include "service.h"

#include <windows.h>

#include <atomic>
#include <cctype>

#include "common.h"
#include "inject.h"
#include "update.h"

namespace launcher {
namespace {

HANDLE g_thread = nullptr;
HANDLE g_stop = nullptr;
NotifyFn g_notify = nullptr;

std::atomic<bool> g_injectRequested{false};
std::atomic<bool> g_updateRequested{false};
std::atomic<bool> g_unloadRequested{false};

void Notify(const std::wstring& text) {
    Log(L"Notify: %ls", text.c_str());
    if (g_notify && g_settings.notifications) g_notify(text);
}

std::wstring InstalledFile() { return DllDirectory() + L"\\installed.txt"; }

// installed.txt: version=... / file=...
bool ReadInstalled(std::string& version, std::wstring& file) {
    std::string content;
    if (!ReadFileUtf8(InstalledFile(), content)) return false;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("version=", 0) == 0) version = line.substr(8);
        if (line.rfind("file=", 0) == 0) file = Widen(line.substr(5));
    }
    return !version.empty() && !file.empty();
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void DeleteOldDlls(const std::wstring& keep) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((DllDirectory() + L"\\BedrockQoL-*.dll").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (_wcsicmp(data.cFileName, keep.c_str()) != 0) {
            // Fails harmlessly for a DLL that is still loaded in the game.
            DeleteFileW((DllDirectory() + L"\\" + data.cFileName).c_str());
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

// Asks the loaded DLL to unload itself and waits for it to disappear from the process.
bool UnloadFromGame(DWORD pid) {
    if (LoadedModDll(pid).empty()) return true;
    SendGameCommand("unload");
    for (int i = 0; i < 100; ++i) {
        Sleep(100);
        if (LoadedModDll(pid).empty()) return true;
    }
    return false;
}

// --- Notifications written by the DLL -----------------------------------------------------

long long g_notifyOffset = -1;

void ForwardGameNotifications() {
    const std::wstring path = GameDataDirectory() + L"\\control\\notifications.txt";
    std::string content;
    if (!ReadFileUtf8(path, content)) return;
    const long long size = static_cast<long long>(content.size());
    if (g_notifyOffset < 0) {
        g_notifyOffset = size;  // skip what was there before the launcher started
        return;
    }
    if (size < g_notifyOffset) g_notifyOffset = 0;  // the DLL restarted and truncated the file
    if (size == g_notifyOffset) return;

    std::string fresh = content.substr(static_cast<size_t>(g_notifyOffset));
    const size_t lastNewline = fresh.find_last_of('\n');
    if (lastNewline == std::string::npos) return;  // partial line, wait for the rest
    fresh.resize(lastNewline);
    g_notifyOffset += static_cast<long long>(lastNewline + 1);

    std::string text;
    for (size_t i = 0; i < fresh.size(); ++i) {
        if (fresh[i] == '\\' && i + 1 < fresh.size() && fresh[i + 1] == 'n') {
            text += '\n';
            ++i;
        } else if (fresh[i] != '\r') {
            text += fresh[i];
        }
    }
    if (!text.empty()) Notify(Widen(text));
}

// --- Main loop ---------------------------------------------------------------------------

DWORD WINAPI ServiceThread(LPVOID) {
    DWORD handledPid = 0;
    ULONGLONG nextUpdateCheck = GetTickCount64() + 3000;  // shortly after start

    while (WaitForSingleObject(g_stop, 500) == WAIT_TIMEOUT) {
        ForwardGameNotifications();

        const DWORD pid = FindProcess(g_settings.processName);
        if (!pid) handledPid = 0;

        if (g_unloadRequested.exchange(false)) {
            if (pid && !LoadedModDll(pid).empty()) {
                SendGameCommand("unload");
            } else {
                Notify(L"Мод не загружен в игру");
            }
        }

        if (g_injectRequested.exchange(false)) {
            std::wstring message;
            InjectCurrent(message);
            Notify(message);
            if (pid) handledPid = pid;
        } else if (pid && pid != handledPid && g_settings.autoInject &&
                   ProcessAgeSeconds(pid) >= g_settings.injectDelaySeconds) {
            // Once per game process: if the player unloads the mod with END it stays unloaded.
            handledPid = pid;
            if (LoadedModDll(pid).empty()) {
                std::wstring message;
                const bool ok = InjectCurrent(message);
                Log(L"Auto-inject: %ls", message.c_str());
                if (!ok) Notify(message);
            }
        }

        const bool userCheck = g_updateRequested.exchange(false);
        if (userCheck || (g_settings.updateEnabled && GetTickCount64() >= nextUpdateCheck)) {
            nextUpdateCheck = GetTickCount64() + static_cast<ULONGLONG>(g_settings.checkIntervalMinutes) * 60000ULL;
            std::wstring message;
            const bool changed = CheckForUpdate(true, message);
            if (userCheck || changed) Notify(message);
        }
    }
    return 0;
}

}  // namespace

std::wstring CurrentDllPath() {
    std::string version;
    std::wstring file;
    if (ReadInstalled(version, file) && FileExists(DllDirectory() + L"\\" + file)) {
        return DllDirectory() + L"\\" + file;
    }
    return ExeDirectory() + L"\\BedrockQoL.dll";
}

bool InjectCurrent(std::wstring& message) {
    const DWORD pid = FindProcess(g_settings.processName);
    if (!pid) {
        message = L"Minecraft не запущен";
        return false;
    }
    const std::wstring loaded = LoadedModDll(pid);
    if (!loaded.empty()) {
        message = L"Мод уже загружен в игру (" + loaded + L")";
        return true;
    }
    const std::wstring dll = CurrentDllPath();
    if (!FileExists(dll)) {
        message = L"Файл не найден: " + dll;
        return false;
    }
    if (!GrantAppContainerAccess(dll)) Log(L"Warning: could not set ACL on %ls (%lu)", dll.c_str(), GetLastError());

    std::wstring error;
    if (!InjectDll(pid, dll, error)) {
        message = L"Не удалось загрузить мод: " + error;
        Log(L"%ls", message.c_str());
        return false;
    }
    message = L"Мод загружен в игру (PID " + std::to_wstring(pid) + L")";
    Log(L"Injected %ls into PID %lu", dll.c_str(), pid);
    return true;
}

bool CheckForUpdate(bool reloadIntoGame, std::wstring& message) {
    std::string body;
    std::wstring error;
    if (!HttpGet(g_settings.manifestUrl, body, error)) {
        message = L"Проверка обновлений не удалась: " + error;
        Log(L"%ls (%ls)", message.c_str(), g_settings.manifestUrl.c_str());
        return false;
    }
    Manifest manifest;
    if (!ParseManifest(body, manifest)) {
        message = L"Неверный manifest.txt на сервере обновлений";
        Log(L"%ls", message.c_str());
        return false;
    }

    std::string installedVersion;
    std::wstring installedFile;
    ReadInstalled(installedVersion, installedFile);
    if (manifest.version == installedVersion && FileExists(DllDirectory() + L"\\" + installedFile)) {
        message = L"Установлена последняя версия (" + Widen(installedVersion) + L")";
        Log(L"%ls", message.c_str());
        return false;
    }

    const std::wstring dllUrl = ResolveUrl(g_settings.manifestUrl, Widen(manifest.file));
    std::string dll;
    if (!HttpGet(dllUrl, dll, error)) {
        message = L"Не удалось скачать обновление: " + error;
        Log(L"%ls (%ls)", message.c_str(), dllUrl.c_str());
        return false;
    }
    if (Sha256Hex(dll) != Lower(manifest.sha256)) {
        message = L"Скачанная DLL повреждена (SHA-256 не совпадает), обновление отменено";
        Log(L"%ls", message.c_str());
        return false;
    }

    const std::wstring fileName = L"BedrockQoL-" + Widen(manifest.version) + L".dll";
    const std::wstring path = DllDirectory() + L"\\" + fileName;
    if (!WriteFileUtf8(path + L".part", dll) || !MoveFileExW((path + L".part").c_str(), path.c_str(),
                                                              MOVEFILE_REPLACE_EXISTING)) {
        message = L"Не удалось сохранить обновление в " + path;
        Log(L"%ls", message.c_str());
        return false;
    }
    GrantAppContainerAccess(path);
    WriteFileUtf8(InstalledFile(), "version=" + manifest.version + "\nfile=" + Narrow(fileName) + "\n");
    Log(L"Downloaded version %ls (%zu bytes)", Widen(manifest.version).c_str(), dll.size());
    message = L"Загружена новая версия " + Widen(manifest.version);

    // Swap the running copy: unload the old DLL from the game and inject the new one.
    const DWORD pid = FindProcess(g_settings.processName);
    if (reloadIntoGame && pid && !LoadedModDll(pid).empty()) {
        if (UnloadFromGame(pid)) {
            std::wstring injectMessage;
            InjectCurrent(injectMessage);
            message += L". " + injectMessage;
        } else {
            message += L". Старая версия не выгрузилась, новая загрузится при следующем запуске игры";
        }
    }
    DeleteOldDlls(fileName);
    return true;
}

void StartService(NotifyFn notify) {
    g_notify = notify;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, ServiceThread, nullptr, 0, nullptr);
}

void StopService() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 20000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = nullptr;
}

void RequestInjectNow() { g_injectRequested = true; }
void RequestUpdateCheck() { g_updateRequested = true; }
void RequestUnload() { g_unloadRequested = true; }

}  // namespace launcher
