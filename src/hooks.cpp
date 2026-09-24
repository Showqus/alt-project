#include "hooks.h"

#include <atomic>
#include <string>
#include <vector>

#include "MinHook.h"
#include "chat.h"
#include "config.h"
#include "events.h"
#include "features/autosprint.h"
#include "features/fullbright.h"
#include "features/zoom.h"
#include "game.h"
#include "gui/input.h"
#include "gui/overlay.h"
#include "gui/state.h"
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
// Options::getGamma, 1.21.5x only (the 0x1820 option offset changes between versions).
const std::vector<const char*> kGetGammaSigs = {
    "48 83 EC 28 80 B9 20 18 00 00 00 48 8D 54 24 30 48 8B 01 48 8B 40 60 74 38 41 B8 1A",
};

enum Slot { kSlotFov, kSlotKeyboard, kSlotMouse, kSlotGamma, kSlotCount };

using GetFovFn = float (*)(void* self, float partialTicks, void* a3, void* a4);
using MouseFeedFn = void (*)(void* device, char button, char action, short x, short y, short dx, short dy,
                             char a8);
using GetGammaFn = float (*)(void* options);

GetFovFn g_origGetFov = nullptr;
autosprint::KeyFeedFn g_origKeyboardFeed = nullptr;
MouseFeedFn g_origMouseFeed = nullptr;
GetGammaFn g_origGetGamma = nullptr;

void* g_targets[kSlotCount] = {};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_unloadRequested{false};
std::atomic<ULONGLONG> g_unloadRequestedAt{0};
std::atomic<bool> g_keyboardFallback{false};
std::atomic<unsigned> g_keyboardEvents{0};
HANDLE g_unloadEvent = nullptr;

// Key state seen through Keyboard::feed, used to turn auto-repeat into clean press edges.
bool g_keyState[256] = {};

// Mouse state seen through MouseDevice::feed (input thread), to release held buttons for the menu.
void* g_mouseDevice = nullptr;
bool g_mouseHeld[4] = {};
short g_mouseX = 0;
short g_mouseY = 0;
std::atomic<bool> g_menuReleased{false};

// --- Detours -----------------------------------------------------------------------------

float HookGetFov(void* self, float partialTicks, void* a3, void* a4) {
    const float fov = g_origGetFov(self, partialTicks, a3, a4);
    if (g_unloading.load(std::memory_order_relaxed)) return fov;
    return zoom::OnFov(fov);
}

// Input thread: when the menu opens (menu key, or the .menu command from the worker), the game must
// not keep walking, sprinting, zooming or mining with keys it saw go down before.
void SyncMenu() {
    const bool open = gui::MenuOpen();
    if (g_menuReleased.exchange(open) == open || !open) return;
    if (g_origKeyboardFeed && !g_keyboardFallback.load()) {
        autosprint::Release(g_origKeyboardFeed);
        for (int vk = 1; vk < 256; ++vk) {
            if (g_keyState[vk]) g_origKeyboardFeed(vk, 0);
        }
    }
    zoom::OnKey(false, false);
    if (g_origMouseFeed && g_mouseDevice) {
        for (char button = 1; button <= 3; ++button) {
            if (!g_mouseHeld[static_cast<int>(button)]) continue;
            g_origMouseFeed(g_mouseDevice, button, 0, g_mouseX, g_mouseY, 0, 0, 0);
        }
    }
}

// Runs on the input thread: releases what we hold in the game, then lets the worker unload us.
void BeginUnload() {
    autosprint::Release(g_origKeyboardFeed);
    zoom::Reset();
    g_unloading.store(true);
    SetEvent(g_unloadEvent);
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

    if (g_unloadRequested.load()) {
        BeginUnload();
        g_origKeyboardFeed(key, state);
        return;
    }

    // The menu: its key opens it, and while it is open every key belongs to it.
    SyncMenu();
    if (gui::input::OnKey(vk, down, pressed)) {
        chat::TrackModifiers(vk, down);
        SyncMenu();
        return;
    }

    if (pressed && vk == cfg.unloadKey && !chat::IsOpen()) {
        BeginUnload();
        g_origKeyboardFeed(key, state);
        return;
    }

    // Chat first: it decides whether the chat is open, which gates everything below.
    if (chat::OnKey(vk, down, g_origKeyboardFeed)) return;

    const bool inWorld = game::InWorld();
    if (changed && vk == cfg.zoomKey) {
        static bool hinted = false;
        if (pressed && !inWorld && !chat::IsOpen() && !hinted) {
            hinted = true;
            logx::Info("Zoom key ignored: the mouse cursor is visible (menu open). If this happens while "
                       "you are in the world, set RequireHiddenCursor=0 in config.ini");
        }
        zoom::OnKey(down, inWorld);
    }

    if (pressed && inWorld) {
        events::Event event;
        event.type = events::Type::KeyPress;
        event.vk = vk;
        events::Push(event);
    }

    if (autosprint::OnKeyEvent(vk, down, g_origKeyboardFeed)) return;
    g_origKeyboardFeed(key, state);
}

void HookMouseFeed(void* device, char button, char action, short x, short y, short dx, short dy, char a8) {
    if (!g_unloading.load(std::memory_order_relaxed)) {
        g_mouseDevice = device;
        g_mouseX = x;
        g_mouseY = y;
        if (button >= 1 && button <= 3) g_mouseHeld[static_cast<int>(button)] = action != 0;
        SyncMenu();
        if (gui::input::OnMouse(button, action, x, y, dx, dy)) return;

        // button 4 = mouse wheel, action = signed wheel delta (0x78 = up, 0x88 = down).
        if (button == 4) {
            if (zoom::OnScroll(static_cast<signed char>(action), game::InWorld())) return;
        } else if ((button == 1 || button == 2) && action == 1) {
            chat::OnMouseClick();
        }
    }
    g_origMouseFeed(device, button, action, x, y, dx, dy, a8);
}

float HookGetGamma(void* options) {
    const float gamma = g_origGetGamma(options);
    if (g_unloading.load(std::memory_order_relaxed)) return gamma;
    return fullbright::OnGamma(gamma);
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

    // All hooks are installed regardless of which features are enabled, so features can be
    // switched on later from chat without restarting the game.
    const uintptr_t fov = Resolve("LevelRendererPlayer::getFov", g_config.sigGetFov, kGetFovSigs, text);
    Create(kSlotFov, "LevelRendererPlayer::getFov", fov, reinterpret_cast<void*>(&HookGetFov),
           reinterpret_cast<void**>(&g_origGetFov));

    const uintptr_t mouse = Resolve("MouseDevice::feed", g_config.sigMouseFeed, kMouseFeedSigs, text);
    Create(kSlotMouse, "MouseDevice::feed", mouse, reinterpret_cast<void*>(&HookMouseFeed),
           reinterpret_cast<void**>(&g_origMouseFeed));

    const uintptr_t gamma = Resolve("Options::getGamma", g_config.sigGetGamma, kGetGammaSigs, text);
    Create(kSlotGamma, "Options::getGamma", gamma, reinterpret_cast<void*>(&HookGetGamma),
           reinterpret_cast<void**>(&g_origGetGamma));

    const uintptr_t keyboard = Resolve("Keyboard::feed", g_config.sigKeyboardFeed, kKeyboardFeedSigs, text);
    Create(kSlotKeyboard, "Keyboard::feed", keyboard, reinterpret_cast<void*>(&HookKeyboardFeed),
           reinterpret_cast<void**>(&g_origKeyboardFeed));
    if (!HasKeyboardHook()) {
        logx::Warn("Keyboard hook unavailable - using the polling fallback for keys (no chat commands)");
        g_keyboardFallback.store(true);
    }

    // The in-game menu (IDXGISwapChain::Present).
    gui::overlay::Install();
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

bool HasFovHook() { return g_targets[kSlotFov] != nullptr; }
bool HasKeyboardHook() { return g_targets[kSlotKeyboard] != nullptr; }
bool HasMouseHook() { return g_targets[kSlotMouse] != nullptr; }
bool HasGammaHook() { return g_targets[kSlotGamma] != nullptr; }

void RequestUnload() {
    if (g_keyboardFallback.load() || !HasKeyboardHook()) {
        g_unloading.store(true);
        SetEvent(g_unloadEvent);
        return;
    }
    // Let the input thread release the sprint key on its next key event (see HookKeyboardFeed);
    // the worker forces the unload if no key event arrives in time.
    g_unloadRequestedAt.store(GetTickCount64());
    g_unloadRequested.store(true);
}

bool UnloadRequestTimedOut() {
    const ULONGLONG at = g_unloadRequestedAt.load();
    return g_unloadRequested.load() && at != 0 && GetTickCount64() - at > 1500;
}

unsigned KeyboardEventCount() { return g_keyboardEvents.load(std::memory_order_relaxed); }

void SetKeyboardFallback(bool enabled) { g_keyboardFallback.store(enabled); }
bool KeyboardFallback() { return g_keyboardFallback.load(); }

HANDLE UnloadEvent() { return g_unloadEvent; }

}  // namespace hooks
