// BedrockQoL - AutoSprint + Zoom for Minecraft Bedrock Edition (Windows) 1.21.5x.

#include <windows.h>

#include <string>

#include "config.h"
#include "features/autosprint.h"
#include "features/zoom.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

namespace {

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

void LogGameVersion() {
    const std::string package = Narrow(game::PackageFullName());
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
    const int watched[] = {cfg.unloadKey, cfg.sprintToggleKey, cfg.zoomKey, cfg.forwardKey, cfg.sprintKey,
                           'A', 'S', 'D', VK_SPACE};

    KeyPoller poller;
    unsigned lastHookEvents = hooks::KeyboardEventCount();
    unsigned transitionsWithoutHook = 0;

    while (WaitForSingleObject(unloadEvent, 10) == WAIT_TIMEOUT) {
        for (int vk : watched) poller.Update(vk);
        const bool focused = game::HasFocus();

        if (!hooks::KeyboardFallback()) {
            // Keyboard hook installed: make sure it actually sees key presses. If the player
            // presses keys in the focused game and the hook stays silent, the signature matched
            // the wrong function - switch to polling so the features keep working.
            const unsigned events = hooks::KeyboardEventCount();
            if (events != lastHookEvents) {
                lastHookEvents = events;
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

        const bool inWorld = focused && game::InWorld();

        if (focused && poller.Pressed(cfg.unloadKey)) {
            SetEvent(unloadEvent);
            break;
        }
        if (inWorld && poller.Pressed(cfg.sprintToggleKey)) autosprint::Toggle();
        if (poller.Changed(cfg.zoomKey)) zoom::OnKey(focused && poller.Down(cfg.zoomKey), inWorld);
        autosprint::PollFallback(focused, inWorld);
    }
}

DWORD WINAPI MainThread(LPVOID param) {
    HMODULE module = static_cast<HMODULE>(param);

    const std::wstring dir = game::DataDirectory();
    logx::Init(dir + L"\\BedrockQoL.log");
    logx::Info("BedrockQoL loaded, data folder: %s", Narrow(dir).c_str());
    LogGameVersion();

    config::Load(dir + L"\\config.ini");

    if (hooks::Install()) {
        logx::Info("Ready. Zoom: %s, scroll: %s, keyboard: %s",
                   hooks::HasFovHook() ? "OK" : "unavailable", hooks::HasMouseHook() ? "OK" : "unavailable",
                   hooks::HasKeyboardHook() ? "hook" : "polling fallback");
        WorkerLoop(hooks::UnloadEvent());
    }

    logx::Info("Unloading...");
    autosprint::ReleaseFallback();
    zoom::Reset();
    hooks::Uninstall();
    logx::Info("Unloaded");
    logx::Shutdown();

    FreeLibraryAndExitThread(module, 0);
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
