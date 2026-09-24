#include "input.h"

#include <windows.h>

#include <algorithm>
#include <atomic>

#include "../chat.h"
#include "../config.h"
#include "../events.h"
#include "../game.h"
#include "../hooks.h"
#include "../keys.h"
#include "../log.h"
#include "imgui.h"
#include "overlay.h"
#include "state.h"

namespace gui::input {
namespace {

enum class Kind { Key, MousePos, MouseButton, Wheel };

struct Event {
    Kind kind = Kind::Key;
    KeyEvent key;
    float x = 0.0f;
    float y = 0.0f;
    int button = 0;
    bool down = false;
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Event> g_queue;

// Input-thread state.
bool g_shift = false;
bool g_ctrl = false;
bool g_alt = false;
int g_lastX = -1;
int g_lastY = -1;
float g_posX = 0.0f;
float g_posY = 0.0f;
bool g_wasOpen = false;
bool g_hinted = false;

// Render-thread state for the polling fallbacks.
bool g_polledKeys[256] = {};
bool g_polledButtons[3] = {};

void Push(const Event& event) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_queue.size() < 1024) g_queue.push_back(event);
    ReleaseSRWLockExclusive(&g_lock);
}

void PushPos() {
    Event e;
    e.kind = Kind::MousePos;
    e.x = g_posX;
    e.y = g_posY;
    Push(e);
}

void TrackModifiers(int vk, bool down) {
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT: g_shift = down; break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL: g_ctrl = down; break;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU: g_alt = down; break;
        default: break;
    }
}

// The render thread stopped drawing while the menu was open (renderer lost, game minimised for
// long): give the input back to the game instead of swallowing it forever.
bool MenuUsable() {
    if (overlay::Ready()) return true;
    SetMenuOpen(false);
    return false;
}

ImGuiKey VkToImGuiKey(int vk) {
    if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));
    if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
    if (vk >= VK_F1 && vk <= VK_F24) return static_cast<ImGuiKey>(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));
    switch (vk) {
        case VK_TAB: return ImGuiKey_Tab;
        case VK_LEFT: return ImGuiKey_LeftArrow;
        case VK_RIGHT: return ImGuiKey_RightArrow;
        case VK_UP: return ImGuiKey_UpArrow;
        case VK_DOWN: return ImGuiKey_DownArrow;
        case VK_PRIOR: return ImGuiKey_PageUp;
        case VK_NEXT: return ImGuiKey_PageDown;
        case VK_HOME: return ImGuiKey_Home;
        case VK_END: return ImGuiKey_End;
        case VK_INSERT: return ImGuiKey_Insert;
        case VK_DELETE: return ImGuiKey_Delete;
        case VK_BACK: return ImGuiKey_Backspace;
        case VK_SPACE: return ImGuiKey_Space;
        case VK_RETURN: return ImGuiKey_Enter;
        case VK_ESCAPE: return ImGuiKey_Escape;
        case VK_OEM_7: return ImGuiKey_Apostrophe;
        case VK_OEM_COMMA: return ImGuiKey_Comma;
        case VK_OEM_MINUS: return ImGuiKey_Minus;
        case VK_OEM_PERIOD: return ImGuiKey_Period;
        case VK_OEM_2: return ImGuiKey_Slash;
        case VK_OEM_1: return ImGuiKey_Semicolon;
        case VK_OEM_PLUS: return ImGuiKey_Equal;
        case VK_OEM_4: return ImGuiKey_LeftBracket;
        case VK_OEM_5: return ImGuiKey_Backslash;
        case VK_OEM_6: return ImGuiKey_RightBracket;
        case VK_OEM_3: return ImGuiKey_GraveAccent;
        case VK_CAPITAL: return ImGuiKey_CapsLock;
        case VK_SCROLL: return ImGuiKey_ScrollLock;
        case VK_NUMLOCK: return ImGuiKey_NumLock;
        case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
        case VK_PAUSE: return ImGuiKey_Pause;
        case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
        case VK_DIVIDE: return ImGuiKey_KeypadDivide;
        case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
        case VK_ADD: return ImGuiKey_KeypadAdd;
        case VK_SHIFT:
        case VK_LSHIFT: return ImGuiKey_LeftShift;
        case VK_RSHIFT: return ImGuiKey_RightShift;
        case VK_CONTROL:
        case VK_LCONTROL: return ImGuiKey_LeftCtrl;
        case VK_RCONTROL: return ImGuiKey_RightCtrl;
        case VK_MENU:
        case VK_LMENU: return ImGuiKey_LeftAlt;
        case VK_RMENU: return ImGuiKey_RightAlt;
        case VK_LWIN: return ImGuiKey_LeftSuper;
        case VK_RWIN: return ImGuiKey_RightSuper;
        case VK_APPS: return ImGuiKey_Menu;
        default: return ImGuiKey_None;
    }
}

// Mouse fallback (no MouseDevice::feed hook): read the system cursor and buttons.
void PollMouse(ImGuiIO& io) {
    HWND window = overlay::GameWindow();
    POINT p;
    if (window && GetCursorPos(&p) && ScreenToClient(window, &p)) {
        io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    const int vks[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    for (int i = 0; i < 3; ++i) {
        const bool down = (GetAsyncKeyState(vks[i]) & 0x8000) != 0;
        if (down != g_polledButtons[i]) io.AddMouseButtonEvent(i, down);
        g_polledButtons[i] = down;
    }
}

// Keyboard fallback (no Keyboard::feed hook): key transitions from GetAsyncKeyState.
void PollKeys(std::vector<KeyEvent>& keys) {
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    for (int vk = 8; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down == g_polledKeys[vk]) continue;
        g_polledKeys[vk] = down;
        KeyEvent e;
        e.vk = vk;
        e.down = down;
        if (down) keys::Translate(vk, shift, ctrl, alt, e.chars);
        keys.push_back(e);
    }
}

}  // namespace

bool OnKey(int vk, bool down, bool pressed) {
    TrackModifiers(vk, down);

    if (!MenuOpen()) {
        const int menuKey = g_config.menuKey;
        if (!pressed || menuKey <= 0 || vk != menuKey || chat::IsOpen()) return false;
        if (!overlay::Ready()) {
            if (!g_hinted) {
                g_hinted = true;
                logx::Warn("Menu key pressed, but the menu cannot be drawn: %s", overlay::Status().c_str());
            }
            // The .menu command tells the player why (at most every 2 seconds).
            static ULONGLONG lastExplained = 0;
            const ULONGLONG now = GetTickCount64();
            if (lastExplained == 0 || now - lastExplained > 2000) {
                lastExplained = now;
                events::Event event;
                event.type = events::Type::Command;
                event.text = "menu";
                events::Push(event);
            }
            return false;
        }
        SetMenuOpen(true);
        return true;
    }
    if (!MenuUsable()) return false;

    Event e;
    e.kind = Kind::Key;
    e.key.vk = vk;
    e.key.down = down;
    if (down) keys::Translate(vk, g_shift, g_ctrl, g_alt, e.key.chars);
    Push(e);
    return true;
}

bool OnMouse(int button, int action, int x, int y, int dx, int dy) {
    const bool open = MenuOpen();
    if (open && !g_wasOpen && game::CursorHidden()) {
        // Opened in the world: the game has locked the cursor, start the menu's own cursor in the middle.
        float w = 0.0f, h = 0.0f;
        overlay::DisplaySize(w, h);
        g_posX = w * 0.5f;
        g_posY = h * 0.5f;
    }
    g_wasOpen = open;

    // Absolute position when the game reports a new one, relative movement while the cursor is locked.
    bool moved = false;
    if (x != g_lastX || y != g_lastY) {
        g_lastX = x;
        g_lastY = y;
        g_posX = static_cast<float>(x);
        g_posY = static_cast<float>(y);
        moved = true;
    } else if (button == 0 && (dx != 0 || dy != 0)) {
        float w = 0.0f, h = 0.0f;
        overlay::DisplaySize(w, h);
        g_posX = std::clamp(g_posX + static_cast<float>(dx), 0.0f, std::max(w - 1.0f, 0.0f));
        g_posY = std::clamp(g_posY + static_cast<float>(dy), 0.0f, std::max(h - 1.0f, 0.0f));
        moved = true;
    }

    if (!open || !MenuUsable()) return false;

    if (moved || button != 0) PushPos();
    if (button >= 1 && button <= 3) {
        Event e;
        e.kind = Kind::MouseButton;
        e.button = button == 1 ? 0 : button == 2 ? 1 : 2;  // left, right, middle
        e.down = action != 0;
        Push(e);
    } else if (button == 4) {
        Event e;
        e.kind = Kind::Wheel;
        e.y = static_cast<float>(static_cast<signed char>(action)) / 120.0f;
        Push(e);
    }
    return true;
}

void Drain(ImGuiIO& io, std::vector<KeyEvent>& keys) {
    std::vector<Event> events;
    AcquireSRWLockExclusive(&g_lock);
    events.swap(g_queue);
    ReleaseSRWLockExclusive(&g_lock);

    for (Event& e : events) {
        switch (e.kind) {
            case Kind::Key: keys.push_back(std::move(e.key)); break;
            case Kind::MousePos: io.AddMousePosEvent(e.x, e.y); break;
            case Kind::MouseButton: io.AddMouseButtonEvent(e.button, e.down); break;
            case Kind::Wheel: io.AddMouseWheelEvent(0.0f, e.y); break;
        }
    }

    if (!hooks::HasMouseHook()) PollMouse(io);
    if (!hooks::HasKeyboardHook() || hooks::KeyboardFallback()) PollKeys(keys);
}

void FeedKey(ImGuiIO& io, const KeyEvent& key) {
    switch (key.vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT: io.AddKeyEvent(ImGuiMod_Shift, key.down); break;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL: io.AddKeyEvent(ImGuiMod_Ctrl, key.down); break;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU: io.AddKeyEvent(ImGuiMod_Alt, key.down); break;
        case VK_LWIN:
        case VK_RWIN: io.AddKeyEvent(ImGuiMod_Super, key.down); break;
        default: break;
    }
    const ImGuiKey imguiKey = VkToImGuiKey(key.vk);
    if (imguiKey != ImGuiKey_None) io.AddKeyEvent(imguiKey, key.down);
    if (key.down) {
        for (wchar_t c : key.chars) io.AddInputCharacterUTF16(static_cast<ImWchar16>(c));
    }
}

void Reset(ImGuiIO& io) {
    io.ClearInputKeys();
    io.ClearInputMouse();
    // Keys already held when the menu opens (e.g. the menu key itself) are not new presses.
    for (int vk = 0; vk < 256; ++vk) g_polledKeys[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    for (bool& b : g_polledButtons) b = false;
}

}  // namespace gui::input
