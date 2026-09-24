#include "hooks.h"

#include <atomic>
#include <string>
#include <vector>

#include "MinHook.h"
#include "config.h"
#include "features/autosprint.h"
#include "features/zoom.h"
#include "game.h"
#include "log.h"
#include "scanner.h"

namespace hooks {
namespace {

// Signatures for Minecraft Bedrock 1.21.5x (1.21.50 / 1.21.51), taken from the open-source
// Flarial client (github.com/flarialmc/dll, src/Utils/Memory/Game/Sig/SigInit.cpp), where the
// same patterns are used for every version from 1.20.x up to 1.21.11x.
const std::vector<const char*> kGetFovSigs = {
    "? ? ? ? ? ? ? 48 89 ? ? 57 48 81 EC ? ? ? ? 0F 29 ? ? 0F 29 ? ? 44 0F ? ? ? 44 0F ? ? ? 48 8B ? ? ? ? ? "
    "48 33 ? 48 89 ? ? ? 41 0F",
};
const std::vector<const char*> kKeyboardFeedSigs = {
    "? ? ? ? ? ? ? 4C 8D 05 ? ? ? ? 89 54 24 20 88",
};
const std::vector<const char*> kMouseFeedSigs = {
    "E8 ? ? ? ? 40 88 6C 1F",  // call MouseDevice::feed
};

using GetFovFn = float (*)(void* self, float partialTicks, void* a3, void* a4);
using MouseFeedFn = void (*)(void* device, char button, char action, short x, short y, short dx, short dy,
                             char a8);

GetFovFn g_origGetFov = nullptr;
autosprint::KeyFeedFn g_origKeyboardFeed = nullptr;
MouseFeedFn g_origMouseFeed = nullptr;

void* g_targets[3] = {};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_keyboardFallback{false};
std::atomic<unsigned> g_keyboardEvents{0};
HANDLE g_unloadEvent = nullptr;

// Key state seen through Keyboard::feed, used to turn auto-repeat into clean press edges.
bool g_keyState[256] = {};

// --- Detours -----------------------------------------------------------------------------

float HookGetFov(void* self, float partialTicks, void* a3, void* a4) {
    const float fov = g_origGetFov(self, partialTicks, a3, a4);
    if (g_unloading.load(std::memory_order_relaxed)) return fov;
    return zoom::OnFov(fov);
}

void HookKeyboardFeed(int key, int state) {
    g_keyboardEvents.fetch_add(1, std::memory_order_relaxed);

    if (g_unloading.load() || g_keyboardFallback.load()) {
        g_origKeyboardFeed(key, state);
        return;
    }

    const int vk = key & 0xFF;
    const bool down = (state & 0xFF) != 0;
    const bool changed = g_keyState[vk] != down;
    const bool pressed = down && changed;
    g_keyState[vk] = down;

    const Config& cfg = g_config;

    if (pressed && vk == cfg.unloadKey) {
        autosprint::Release(g_origKeyboardFeed);
        zoom::Reset();
        g_unloading.store(true);
        SetEvent(g_unloadEvent);
        g_origKeyboardFeed(key, state);
        return;
    }

    if (pressed && vk == cfg.sprintToggleKey && game::InWorld()) autosprint::Toggle();
    if (changed && vk == cfg.zoomKey) {
        const bool inWorld = game::InWorld();
        static bool hinted = false;
        if (pressed && !inWorld && !hinted) {
            hinted = true;
            logx::Info("Zoom key ignored: the mouse cursor is visible (menu/chat open). If this happens while "
                       "you are in the world, set RequireHiddenCursor=0 in config.ini");
        }
        zoom::OnKey(down, inWorld);
    }

    if (autosprint::OnKeyEvent(vk, down, g_origKeyboardFeed)) return;
    g_origKeyboardFeed(key, state);
}

void HookMouseFeed(void* device, char button, char action, short x, short y, short dx, short dy, char a8) {
    // button 4 = mouse wheel, action = signed wheel delta (0x78 = up, 0x88 = down).
    if (button == 4 && !g_unloading.load(std::memory_order_relaxed)) {
        if (zoom::OnScroll(static_cast<signed char>(action), game::InWorld())) return;
    }
    g_origMouseFeed(device, button, action, x, y, dx, dy, a8);
}

// --- Installation ------------------------------------------------------------------------

uintptr_t Resolve(const char* name, const std::string& override, const std::vector<const char*>& builtin,
                  const scanner::Range& text) {
    std::vector<std::string> candidates;
    if (!override.empty()) candidates.push_back(override);
    for (const char* sig : builtin) candidates.push_back(sig);

    for (size_t i = 0; i < candidates.size(); ++i) {
        const uintptr_t address = scanner::Find(candidates[i], text);
        if (!address) continue;
        if (!text.Contains(address)) {
            logx::Warn("%s: pattern #%zu resolved outside .text, ignoring", name, i);
            continue;
        }
        // MSVC pads between functions with int3/ret, so a direct function pattern should land right
        // after one of those. (Call-site patterns resolve to the callee and are not checked.)
        const bool callSite = candidates[i].rfind("E8", 0) == 0 || candidates[i].rfind("E9", 0) == 0;
        const unsigned char before = *reinterpret_cast<const unsigned char*>(address - 1);
        const bool boundary = callSite || before == 0xCC || before == 0xC3 || before == 0x90;

        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        logx::Info("%s found at Minecraft.Windows.exe+0x%llX (%s pattern #%zu)%s", name,
                   static_cast<unsigned long long>(address - base),
                   (!override.empty() && i == 0) ? "config" : "built-in", i,
                   boundary ? "" : " [warning: not at a function boundary]");
        return address;
    }

    logx::Error("%s: signature not found - this game version is probably not supported", name);
    return 0;
}

bool Create(int slot, const char* name, uintptr_t target, void* detour, void** original) {
    if (!target) return false;
    MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(target), detour, original);
    if (status == MH_OK) status = MH_EnableHook(reinterpret_cast<void*>(target));
    if (status != MH_OK) {
        logx::Error("%s: hook failed (%s)", name, MH_StatusToString(status));
        MH_RemoveHook(reinterpret_cast<void*>(target));
        *original = nullptr;
        return false;
    }
    g_targets[slot] = reinterpret_cast<void*>(target);
    logx::Info("%s hooked", name);
    return true;
}

}  // namespace

bool Install() {
    g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK) {
        logx::Error("MH_Initialize failed (%s)", MH_StatusToString(init));
        return false;
    }

    scanner::Range text;
    if (!scanner::GetTextSection(text)) {
        logx::Error("Could not locate the game's .text section");
        return false;
    }
    logx::Info(".text: 0x%llX bytes", static_cast<unsigned long long>(text.size));

    if (g_config.zoomEnabled) {
        const uintptr_t fov = Resolve("LevelRendererPlayer::getFov", g_config.sigGetFov, kGetFovSigs, text);
        Create(0, "LevelRendererPlayer::getFov", fov, reinterpret_cast<void*>(&HookGetFov),
               reinterpret_cast<void**>(&g_origGetFov));

        if (g_config.zoomScrollAdjust) {
            const uintptr_t mouse = Resolve("MouseDevice::feed", g_config.sigMouseFeed, kMouseFeedSigs, text);
            Create(2, "MouseDevice::feed", mouse, reinterpret_cast<void*>(&HookMouseFeed),
                   reinterpret_cast<void**>(&g_origMouseFeed));
        }
    }

    const uintptr_t keyboard = Resolve("Keyboard::feed", g_config.sigKeyboardFeed, kKeyboardFeedSigs, text);
    Create(1, "Keyboard::feed", keyboard, reinterpret_cast<void*>(&HookKeyboardFeed),
           reinterpret_cast<void**>(&g_origKeyboardFeed));
    if (!HasKeyboardHook()) {
        logx::Warn("Keyboard hook unavailable - using the polling fallback for keys");
        g_keyboardFallback.store(true);
    }
    return true;
}

void Uninstall() {
    g_unloading.store(true);
    MH_DisableHook(MH_ALL_HOOKS);
    // Give threads that are currently inside a detour time to leave it before the code is unmapped.
    Sleep(250);
    MH_Uninitialize();
    if (g_unloadEvent) {
        CloseHandle(g_unloadEvent);
        g_unloadEvent = nullptr;
    }
}

bool HasFovHook() { return g_targets[0] != nullptr; }
bool HasKeyboardHook() { return g_targets[1] != nullptr; }
bool HasMouseHook() { return g_targets[2] != nullptr; }

unsigned KeyboardEventCount() { return g_keyboardEvents.load(std::memory_order_relaxed); }

void SetKeyboardFallback(bool enabled) { g_keyboardFallback.store(enabled); }
bool KeyboardFallback() { return g_keyboardFallback.load(); }

HANDLE UnloadEvent() { return g_unloadEvent; }

}  // namespace hooks
