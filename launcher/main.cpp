// BedrockQoL launcher: runs in the background without a window (tray icon optional), injects the
// mod whenever Minecraft starts and keeps the DLL up to date from GitHub.
//
//   BedrockQoLLauncher.exe                 background mode (tray icon, auto-inject, auto-update)
//   BedrockQoLLauncher.exe --no-tray       fully headless background mode
//   BedrockQoLLauncher.exe --inject        inject once and exit
//   BedrockQoLLauncher.exe --check-update  download the latest DLL (and reload it into the game) and exit
//   BedrockQoLLauncher.exe --cmd "bind K zoom"   run a mod command in the game and exit
//   add --console to any of them to see the log in a console window

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "common.h"
#include "inject.h"
#include "service.h"

using namespace launcher;

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kNotifyMessage = WM_APP + 2;
constexpr UINT kTrayId = 1;

enum MenuId : UINT {
    kMenuInject = 100,
    kMenuUpdate,
    kMenuUnload,
    kMenuOpenFolder,
    kMenuImport,
    kMenuExport,
    kMenuBackground,
    kMenuFont,
    kMenuAutostart,
    kMenuExit,
};

HWND g_window = nullptr;
bool g_trayVisible = false;

const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t kRunValue[] = L"BedrockQoL";

std::wstring ExePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool AutostartEnabled() {
    wchar_t value[MAX_PATH * 2];
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS;
}

void SetAutostart(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enabled) {
        const std::wstring command = L"\"" + ExePath() + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}

void ShowBalloon(const std::wstring& text) {
    if (!g_trayVisible) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_window;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy(nid.szInfoTitle, L"BedrockQoL", ARRAYSIZE(nid.szInfoTitle) - 1);
    wcsncpy(nid.szInfo, text.c_str(), ARRAYSIZE(nid.szInfo) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// Called on the service thread: hand the text to the UI thread.
void NotifyFromService(const std::wstring& text) {
    if (g_window) PostMessageW(g_window, kNotifyMessage, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

std::wstring Stem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name.resize(dot);
    for (wchar_t& c : name) {
        if (c == L' ') c = L'_';
    }
    return name;
}

void ImportConfig() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"Конфиг BedrockQoL (*.ini)\0*.ini\0Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Импорт конфига BedrockQoL";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    const std::wstring name = Stem(file);
    const std::wstring data = GameDataDirectory();
    CreateDirectoryW(data.c_str(), nullptr);
    for (const wchar_t* folder : {L"imports", L"configs"}) {
        const std::wstring dir = data + L"\\" + folder;
        CreateDirectoryW(dir.c_str(), nullptr);
        CopyFileW(file, (dir + L"\\" + name + L".ini").c_str(), FALSE);
    }

    const DWORD pid = FindProcess(g_settings.processName);
    if (pid && !LoadedModDll(pid).empty()) {
        SendGameCommand("config import " + Narrow(name));  // the mod loads it and reports back
    } else {
        CopyFileW(file, (data + L"\\config.ini").c_str(), FALSE);
        ShowBalloon(L"Конфиг '" + name + L"' импортирован и будет использован при следующем запуске мода");
    }
}

void ExportConfig() {
    const std::wstring source = GameDataDirectory() + L"\\config.ini";
    if (!FileExists(source)) {
        ShowBalloon(L"config.ini ещё не создан: запустите мод хотя бы раз");
        return;
    }
    wchar_t file[MAX_PATH] = L"BedrockQoL-config.ini";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"Конфиг BedrockQoL (*.ini)\0*.ini\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"ini";
    ofn.lpstrTitle = L"Экспорт конфига BedrockQoL";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    ShowBalloon(CopyFileW(source.c_str(), file, FALSE) ? L"Конфиг экспортирован: " + std::wstring(file)
                                                       : L"Не удалось экспортировать конфиг");
}

// The game runs sandboxed (UWP) and can only read files inside its own folder, so pictures and fonts
// for the in-game menu are copied into BedrockQoL\images / BedrockQoL\fonts and selected there.
void ImportMenuFile(bool font) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = font ? L"Шрифты (*.ttf, *.otf)\0*.ttf;*.otf;*.ttc\0Все файлы\0*.*\0"
                           : L"Картинки (*.png, *.jpg, *.bmp, *.tga, *.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif\0"
                             L"Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = font ? L"Шрифт для меню BedrockQoL" : L"Фон для меню BedrockQoL";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring name = file;
    const size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    const std::wstring data = GameDataDirectory();
    const std::wstring dir = data + (font ? L"\\fonts" : L"\\images");
    CreateDirectoryW(data.c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!CopyFileW(file, (dir + L"\\" + name).c_str(), FALSE)) {
        ShowBalloon(L"Не удалось скопировать файл в " + dir);
        return;
    }

    const std::wstring key = font ? L"Font" : L"Background";
    const DWORD pid = FindProcess(g_settings.processName);
    if (pid && !LoadedModDll(pid).empty()) {
        SendGameCommand("set Menu." + Narrow(key) + " " + Narrow(name));  // the mod reloads and reports back
    } else {
        WritePrivateProfileStringW(L"Menu", key.c_str(), name.c_str(), (data + L"\\config.ini").c_str());
        ShowBalloon((font ? L"Шрифт меню: " : L"Фон меню: ") + name);
    }
}

void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuInject, L"Загрузить мод в игру");
    AppendMenuW(menu, MF_STRING, kMenuUpdate, L"Проверить обновления");
    AppendMenuW(menu, MF_STRING, kMenuUnload, L"Выгрузить мод из игры");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuOpenFolder, L"Открыть папку настроек");
    AppendMenuW(menu, MF_STRING, kMenuImport, L"Импорт конфига...");
    AppendMenuW(menu, MF_STRING, kMenuExport, L"Экспорт конфига...");
    AppendMenuW(menu, MF_STRING, kMenuBackground, L"Фон для меню игры...");
    AppendMenuW(menu, MF_STRING, kMenuFont, L"Шрифт для меню игры...");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Запускать вместе с Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Выход");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_window, nullptr);
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case kTrayMessage:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) ShowMenu();
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) RequestInjectNow();
            return 0;
        case kNotifyMessage: {
            auto* text = reinterpret_cast<std::wstring*>(lParam);
            ShowBalloon(*text);
            delete text;
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuInject: RequestInjectNow(); break;
                case kMenuUpdate: RequestUpdateCheck(); break;
                case kMenuUnload: RequestUnload(); break;
                case kMenuOpenFolder: {
                    const std::wstring dir = GameDataDirectory();
                    CreateDirectoryW(dir.c_str(), nullptr);
                    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
                case kMenuImport: ImportConfig(); break;
                case kMenuExport: ExportConfig(); break;
                case kMenuBackground: ImportMenuFile(false); break;
                case kMenuFont: ImportMenuFile(true); break;
                case kMenuAutostart: SetAutostart(!AutostartEnabled()); break;
                case kMenuExit: DestroyWindow(hwnd); break;
                default: break;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_window;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy(nid.szTip, L"BedrockQoL (правый клик - меню)", ARRAYSIZE(nid.szTip) - 1);
    g_trayVisible = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void RemoveTrayIcon() {
    if (!g_trayVisible) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_window;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayVisible = false;
}

void OpenConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
    EnableConsoleLog();
}

int RunBackground(HINSTANCE instance) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\BedrockQoLLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Log(L"Another launcher instance is already running");
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"BedrockQoLLauncher";
    RegisterClassW(&wc);
    // Hidden window: only receives tray and service messages.
    g_window = CreateWindowExW(0, wc.lpszClassName, L"BedrockQoL", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                               instance, nullptr);
    if (g_settings.tray) AddTrayIcon();

    Log(L"Launcher started (auto-inject %d, updates %d every %d min, manifest %ls)", g_settings.autoInject,
        g_settings.updateEnabled, g_settings.checkIntervalMinutes, g_settings.manifestUrl.c_str());
    StartService(&NotifyFromService);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopService();
    RemoveTrayIcon();
    Log(L"Launcher stopped");
    if (mutex) CloseHandle(mutex);
    return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    if (argv) LocalFree(argv);

    bool console = false;
    bool noTray = false;
    std::wstring mode;
    std::wstring command;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--console") {
            console = true;
        } else if (a == L"--no-tray") {
            noTray = true;
        } else if (a == L"--inject" || a == L"--check-update") {
            mode = a;
        } else if (a == L"--cmd") {
            mode = a;
            for (size_t j = i + 1; j < args.size(); ++j) command += (command.empty() ? L"" : L" ") + args[j];
            break;
        }
    }

    if (console || !mode.empty()) OpenConsole();
    LoadSettings();
    if (noTray) g_settings.tray = false;

    if (mode == L"--cmd") {
        if (command.empty()) {
            Log(L"Usage: BedrockQoLLauncher.exe --cmd \"bind K zoom\"");
            return 1;
        }
        return SendGameCommand(Narrow(command)) ? 0 : 1;
    }
    if (mode == L"--inject") {
        std::wstring message;
        const bool ok = InjectCurrent(message);
        if (!ok) Log(L"%ls", message.c_str());
        return ok ? 0 : 1;
    }
    if (mode == L"--check-update") {
        std::wstring message;
        CheckForUpdate(true, message);  // logs the result itself
        return 0;
    }
    return RunBackground(instance);
}
