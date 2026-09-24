#include "chat.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "config.h"
#include "events.h"
#include "game.h"
#include "log.h"
#include "text.h"

namespace chat {
namespace {

// Input-thread state.
bool g_shift = false;
bool g_ctrl = false;
bool g_alt = false;

std::atomic<bool> g_open{false};
bool g_valid = true;       // false once the text can no longer be tracked reliably
bool g_selectAll = false;  // after Ctrl+A the next edit replaces everything
std::wstring g_buffer;
size_t g_cursor = 0;
ULONGLONG g_openedAt = 0;

bool IsWordChar(wchar_t c) { return c != L' ' && c != L'\t'; }

void Open(const std::wstring& initial) {
    g_open = true;
    g_valid = true;
    g_selectAll = false;
    g_buffer = initial;
    g_cursor = g_buffer.size();
    g_openedAt = GetTickCount64();
}

void ClearIfSelected() {
    if (!g_selectAll) return;
    g_buffer.clear();
    g_cursor = 0;
    g_selectAll = false;
}

void Insert(const std::wstring& chars) {
    ClearIfSelected();
    g_buffer.insert(g_cursor, chars);
    g_cursor += chars.size();
}

void Paste() {
    std::wstring clip;
    bool ok = false;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* p = static_cast<const wchar_t*>(GlobalLock(data))) {
                clip = p;
                ok = true;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    if (!ok) {
        g_valid = false;
        return;
    }
    // The chat box is single-line.
    for (wchar_t& c : clip) {
        if (c == L'\r' || c == L'\n') c = L' ';
    }
    Insert(clip);
}

void TypeCharacter(int vk) {
    BYTE state[256] = {};
    if (g_shift) state[VK_SHIFT] = state[VK_LSHIFT] = 0x80;
    if (g_ctrl) state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
    if (g_alt) state[VK_MENU] = state[VK_RMENU] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 0x01;
    if (GetKeyState(VK_NUMLOCK) & 1) state[VK_NUMLOCK] = 0x01;

    HKL layout = GetKeyboardLayout(0);
    const UINT scan = MapVirtualKeyExW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC, layout);
    wchar_t out[8] = {};
    // Flag 0x4: do not change the keyboard (dead key) state of the thread.
    const int n = ToUnicodeEx(static_cast<UINT>(vk), scan, state, out, 8, 0x4, layout);
    if (n < 0) {
        g_valid = false;  // dead key: the composed character is unknown
    } else if (n > 0) {
        std::wstring chars;
        for (int i = 0; i < n; ++i) {
            if (out[i] >= 0x20) chars += out[i];
        }
        if (!chars.empty()) Insert(chars);
    }
}

bool Submit(KeyFeedFn feed) {
    g_open = false;
    if (!g_valid || !g_config.chatCommands) return false;

    const std::string message = text::Trim(text::Narrow(g_buffer), " \t");
    const std::string prefix = config::Prefix();
    if (!text::StartsWith(message, prefix)) return false;

    // Do not let the message reach the server: drop Enter and close the chat with Escape.
    feed(VK_ESCAPE, 1);
    feed(VK_ESCAPE, 0);

    events::Event event;
    event.type = events::Type::Command;
    event.text = message.substr(prefix.size());
    events::Push(event);
    return true;
}

}  // namespace

bool OnKey(int vk, bool down, KeyFeedFn feed) {
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
    if (!down) return false;

    if (g_open && g_config.requireHiddenCursor && game::CursorHidden() && GetTickCount64() - g_openedAt > 400) {
        // The chat was closed some other way (mouse click on "X", screen change...).
        g_open = false;
    }

    if (!g_open) {
        const bool canOpen = !g_config.requireHiddenCursor || game::CursorHidden();
        if (canOpen && !g_ctrl && !g_alt) {
            if (vk == g_config.chatOpenKey) {
                Open(L"");
            } else if (vk == g_config.chatCommandKey) {
                Open(L"/");
            }
        }
        return false;
    }

    const bool ctrlOnly = g_ctrl && !g_alt;
    switch (vk) {
        case VK_ESCAPE: g_open = false; return false;
        case VK_RETURN: return Submit(feed);
        case VK_BACK:
            if (g_selectAll) {
                ClearIfSelected();
            } else if (ctrlOnly) {
                size_t start = g_cursor;
                while (start > 0 && !IsWordChar(g_buffer[start - 1])) --start;
                while (start > 0 && IsWordChar(g_buffer[start - 1])) --start;
                g_buffer.erase(start, g_cursor - start);
                g_cursor = start;
            } else if (g_cursor > 0) {
                g_buffer.erase(--g_cursor, 1);
            }
            return false;
        case VK_DELETE:
            if (g_selectAll) {
                ClearIfSelected();
            } else if (g_cursor < g_buffer.size()) {
                g_buffer.erase(g_cursor, 1);
            }
            return false;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
            if (g_shift || ctrlOnly) {
                g_valid = false;  // selection / word jumps: not tracked
            } else if (g_selectAll) {
                g_selectAll = false;
                g_cursor = (vk == VK_LEFT || vk == VK_HOME) ? 0 : g_buffer.size();
            } else if (vk == VK_LEFT) {
                if (g_cursor > 0) --g_cursor;
            } else if (vk == VK_RIGHT) {
                if (g_cursor < g_buffer.size()) ++g_cursor;
            } else {
                g_cursor = vk == VK_HOME ? 0 : g_buffer.size();
            }
            return false;
        case VK_UP:
        case VK_DOWN:
        case VK_TAB:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_PACKET: g_valid = false; return false;  // history, autocomplete, injected text
        default: break;
    }

    if (ctrlOnly) {
        if (vk == 'A') {
            g_selectAll = true;
        } else if (vk == 'V') {
            Paste();
        } else if (vk == 'X') {
            ClearIfSelected();
        } else if (vk == 'Z' || vk == 'Y') {
            g_valid = false;
        }
        return false;
    }

    TypeCharacter(vk);
    return false;
}

void OnMouseClick() {
    if (g_open) g_valid = false;
}

bool IsOpen() { return g_open.load(std::memory_order_relaxed); }

}  // namespace chat
