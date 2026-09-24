#include "autosprint.h"

#include <windows.h>

#include <atomic>

#include "../config.h"
#include "../game.h"
#include "../log.h"

namespace autosprint {
namespace {

std::atomic<int> g_enabled{-1};  // -1 = take the value from the config on first use

// Hook-mode state, only touched on the game's input thread.
bool g_forwardDown = false;
bool g_sprintPhysicallyDown = false;
bool g_injected = false;  // the game currently believes the sprint key is held because of us

// Fallback-mode state, only touched on the worker thread.
bool g_sendInputHeld = false;

bool Enabled() {
    int v = g_enabled.load();
    if (v < 0) {
        v = g_config.sprintEnabled ? 1 : 0;
        g_enabled.store(v);
    }
    return v == 1;
}

bool Configured() {
    return g_config.forwardKey > 0 && g_config.sprintKey > 0 && g_config.forwardKey != g_config.sprintKey;
}

void SendKey(int vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

}  // namespace

void Toggle() {
    const bool now = !Enabled();
    g_enabled.store(now ? 1 : 0);
    logx::Info("AutoSprint %s", now ? "enabled" : "disabled");
}

bool OnKeyEvent(int vk, bool down, KeyFeedFn feed) {
    if (!Configured()) return false;

    if (vk == g_config.sprintKey) g_sprintPhysicallyDown = down;
    if (vk == g_config.forwardKey) g_forwardDown = down;

    // Re-evaluated on every key event, before the event reaches the game, so that opening
    // chat/inventory drops the injected sprint key before the next typed key is processed.
    const bool want = Enabled() && g_forwardDown && game::InWorld();

    if (vk == g_config.sprintKey) {
        // The player let go of the real sprint key while we are auto-sprinting: keep it held.
        if (!down && want && g_injected) return true;
        if (!down) g_injected = false;
        return false;
    }

    if (want) {
        // Re-press on every forward key press as well, in case the game dropped its key state
        // (focus loss, menu) while we still thought the key was injected.
        if (!g_injected || (vk == g_config.forwardKey && down)) {
            feed(g_config.sprintKey, 1);
            g_injected = true;
        }
    } else if (g_injected) {
        g_injected = false;
        if (!g_sprintPhysicallyDown) feed(g_config.sprintKey, 0);
    }
    return false;
}

void Release(KeyFeedFn feed) {
    if (g_injected && !g_sprintPhysicallyDown) feed(g_config.sprintKey, 0);
    g_injected = false;
}

void PollFallback(bool focused, bool inWorld) {
    if (!g_config.sprintFallbackSendInput || !Configured()) return;

    const bool forward = (GetAsyncKeyState(g_config.forwardKey) & 0x8000) != 0;
    const bool want = Enabled() && focused && inWorld && forward;

    if (want && !g_sendInputHeld) {
        SendKey(g_config.sprintKey, true);
        g_sendInputHeld = true;
    } else if (!want && g_sendInputHeld) {
        SendKey(g_config.sprintKey, false);
        g_sendInputHeld = false;
    }
}

void ReleaseFallback() {
    if (g_sendInputHeld) SendKey(g_config.sprintKey, false);
    g_sendInputHeld = false;
}

}  // namespace autosprint
