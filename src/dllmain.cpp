// BedrockQoL - AutoSprint, Zoom, Fullbright, TextHotkey and chat commands for
// Minecraft Bedrock Edition (Windows) 1.21.5x.

#include <windows.h>

#include <cstdio>
#include <string>

#include "commands.h"
#include "config.h"
#include "crashlog.h"
#include "events.h"
#include "features/autosprint.h"
#include "features/zoom.h"
#include "game.h"
#include "gui/overlay.h"
#include "gui/state.h"
#include "hooks.h"
#include "keys.h"
#include "log.h"
#include "notify.h"
#include "text.h"

#ifndef BEDROCKQOL_VERSION
#define BEDROCKQOL_VERSION "dev"
#endif

namespace {

void LogGameVersion() {
    const std::string package = text::Narrow(game::PackageFullName());
    if (package.empty()) {
        logx::Warn("Game package not detected (not the UWP build?) - signatures may not match");
        return;
    }
    // Microsoft.MinecraftUWP_1.21.5101.0_x64__8wekyb3d8bbwe -> 1.21.5101.0
    std::string version = package;
    const size_t first = package.find('_');
    if (first != std::string::npos) {
        const size_t second = package.find('_', first + 1);
        version = package.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    }
    logx::Info("Game package: %s", package.c_str());
    if (version.rfind("1.21.5", 0) != 0) {
        logx::Warn("Game version %s: this build targets 1.21.5x (1.21.51), features may not work", version.c_str());
    }
}

std::wstring ControlPath(const wchar_t* name) { return config::Directory() + L"\\control\\" + name; }

// Tells the launcher which build is loaded into which process (and whether the menu works).
void WriteStatus(const std::string& menu) {
    if (FILE* f = _wfopen(ControlPath(L"status.txt").c_str(), L"wb")) {
        fprintf(f, "version=%s\npid=%lu\nmenu=%s\n", BEDROCKQOL_VERSION, GetCurrentProcessId(), menu.c_str());
        fclose(f);
    }
}

// Commands written by the launcher (BedrockQoLLauncher.exe --cmd "..."), one per line.
void RunControlCommands() {
    const std::wstring path = ControlPath(L"commands.txt");
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return;
    std::string content;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) content.append(buffer, n);
    fclose(f);
    DeleteFileW(path.c_str());

    const std::string prefix = config::Prefix();
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        std::string line = text::Trim(content.substr(pos, end - pos), " \t\r\xEF\xBB\xBF");
        pos = end + 1;
        if (text::StartsWith(line, prefix)) line = line.substr(prefix.size());
        if (!line.empty()) commands::Execute(line);
    }
}

// Seconds since the game process started.
double ProcessAgeSeconds() {
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 1e9;
    GetSystemTimeAsFileTime(&now);
    const auto ticks = [](const FILETIME& t) {
        return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    return static_cast<double>(ticks(now) - ticks(created)) / 1e7;
}

// Edge detection on top of GetAsyncKeyState for the polling fallback.
class KeyPoller {
public:
    void Update(int vk) {
        if (vk <= 0 || vk > 255) return;
        prev_[vk] = cur_[vk];
        cur_[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    bool Down(int vk) const { return vk > 0 && vk < 256 && cur_[vk]; }
    bool Pressed(int vk) const { return vk > 0 && vk < 256 && cur_[vk] && !prev_[vk]; }
    bool Changed(int vk) const { return vk > 0 && vk < 256 && cur_[vk] != prev_[vk]; }

private:
    bool cur_[256] = {};
    bool prev_[256] = {};
};

void WorkerLoop(HANDLE unloadEvent) {
    const Config& cfg = g_config;
    KeyPoller poller;
    unsigned lastHookEvents = hooks::KeyboardEventCount();
    unsigned transitionsWithoutHook = 0;
    ULONGLONG lastControlCheck = 0;
    std::string menuStatus = "unavailable";
    bool menuEnabled = cfg.menuEnabled;
    bool menuStarted = false;  // gui::overlay::Start was called (it runs once the game had time to set up)

    while (WaitForSingleObject(unloadEvent, 10) == WAIT_TIMEOUT) {
        events::Event event;
        while (events::Pop(event)) {
            if (event.type == events::Type::Command) {
                commands::Execute(event.text);
            } else if (event.type == events::Type::ConfigWrite) {
                gui::SetAppliedWrite(event.writeId);  // reported by the snapshot published after the reload
                config::Apply(event.entries);
            } else {
                commands::OnKeyPress(event.vk);
            }
        }

        if (hooks::UnloadRequestTimedOut()) {
            SetEvent(unloadEvent);
            break;
        }

        const ULONGLONG now = GetTickCount64();
        if (now - lastControlCheck >= 250) {
            lastControlCheck = now;
            RunControlCommands();
            // The menu hooks Direct3D once the game has had time to set up its own renderer, and again
            // when [Menu] Enabled is switched on while the game runs (e.g. after the crash guard).
            if (cfg.menuEnabled && !menuEnabled) menuStarted = false;
            menuEnabled = cfg.menuEnabled;
            if (!menuStarted && ProcessAgeSeconds() >= cfg.menuStartDelay) {
                menuStarted = true;
                gui::overlay::Start();
            }
            const std::string menu = gui::overlay::Ready() ? gui::overlay::Status() : "unavailable";
            if (menu != menuStatus) {
                menuStatus = menu;
                WriteStatus(menuStatus);
            }
        }

        const bool focused = game::HasFocus();

        if (!hooks::KeyboardFallback()) {
            const int watched[] = {cfg.forwardKey, cfg.zoomKey, 'A', 'S', 'D', VK_SPACE};
            for (int vk : watched) poller.Update(vk);
            // Keyboard hook installed: make sure it actually sees key presses. If the player
            // presses keys in the focused game and the hook stays silent, the signature matched
            // the wrong function - switch to polling so the features keep working.
            const unsigned hookEvents = hooks::KeyboardEventCount();
            if (hookEvents != lastHookEvents) {
                lastHookEvents = hookEvents;
                transitionsWithoutHook = 0;
            } else if (focused) {
                for (int vk : watched) {
                    if (poller.Changed(vk)) ++transitionsWithoutHook;
                }
                if (transitionsWithoutHook >= 8) {
                    logx::Warn("Keyboard hook receives no events - switching to the polling fallback");
                    hooks::SetKeyboardFallback(true);
                }
            }
            continue;
        }

        // Polling fallback: no chat commands (Enter cannot be intercepted), everything else works.
        for (int vk = 1; vk < 256; ++vk) poller.Update(vk);
        const bool inWorld = focused && game::InWorld();
        if (focused && poller.Pressed(cfg.menuKey) && cfg.menuKey > 0) {
            gui::SetMenuOpen(!gui::MenuOpen() && gui::overlay::Ready());
        }
        if (focused && !gui::MenuOpen() && poller.Pressed(cfg.unloadKey)) {
            SetEvent(unloadEvent);
            break;
        }
        if (poller.Changed(cfg.zoomKey)) zoom::OnKey(focused && poller.Down(cfg.zoomKey), inWorld);
        if (inWorld) {
            for (int vk = 1; vk < 256; ++vk) {
                if (poller.Pressed(vk)) commands::OnKeyPress(vk);
            }
        }
        autosprint::PollFallback(focused, inWorld);
    }
}

DWORD WINAPI MainThread(LPVOID param) {
    HMODULE module = static_cast<HMODULE>(param);

    const std::wstring dir = game::DataDirectory();
    // Keep the log of the previous session: if the game crashed, that is the one that tells why.
    MoveFileExW((dir + L"\\BedrockQoL.log").c_str(), (dir + L"\\BedrockQoL.prev.log").c_str(),
                MOVEFILE_REPLACE_EXISTING);
    logx::Init(dir + L"\\BedrockQoL.log");
    crashlog::Install(module);
    logx::Info("BedrockQoL %s loaded, data folder: %s", BEDROCKQOL_VERSION, text::Narrow(dir).c_str());
    LogGameVersion();

    notify::Init(dir);
    config::Init(dir);

    if (hooks::Install()) {
        logx::Info("Ready. Zoom: %s, Fullbright: %s, mouse: %s, keyboard: %s",
                   hooks::HasFovHook() ? "OK" : "unavailable", hooks::HasGammaHook() ? "OK" : "unavailable",
                   hooks::HasMouseHook() ? "OK" : "unavailable",
                   hooks::HasKeyboardHook() ? "hook" : "polling fallback");
        WriteStatus("unavailable");
        notify::Send(std::string("BedrockQoL ") + BEDROCKQOL_VERSION + " загружен. Меню: " +
                     keys::Name(g_config.menuKey) + ", команды: " + config::Prefix() + "help");
        WorkerLoop(hooks::UnloadEvent());
    }

    logx::Info("Unloading...");
    gui::overlay::Shutdown();
    autosprint::ReleaseFallback();
    zoom::Reset();
    hooks::Uninstall();
    DeleteFileW(ControlPath(L"status.txt").c_str());
    logx::Info("Unloaded");
    crashlog::Uninstall();
    logx::Shutdown();

    FreeLibraryAndExitThread(module, 0);
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH && reserved) {
        gui::overlay::OnProcessExit();  // the game closes normally
    }
    return TRUE;
}
