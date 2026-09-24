#include "texthotkey.h"

#include <windows.h>

#include <vector>

#include "../commands.h"
#include "../config.h"
#include "../game.h"
#include "../log.h"
#include "../notify.h"
#include "../text.h"

namespace texthotkey {
namespace {

ULONGLONG g_lastSent = 0;

void Key(WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

void TypeUnicode(const std::wstring& s) {
    std::vector<INPUT> inputs;
    inputs.reserve(s.size() * 2);
    for (wchar_t c : s) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = c;
        input.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(input);
        input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(input);
    }
    if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

}  // namespace

bool SendChatMessage(const std::string& utf8Text) {
    if (!game::HasFocus()) {
        logx::Warn("TextHotkey: game window is not focused, message not sent");
        return false;
    }

    // Open the chat, wait for it to show the cursor, type, send.
    Key(static_cast<WORD>(g_config.chatOpenKey.load()), true);
    Key(static_cast<WORD>(g_config.chatOpenKey.load()), false);
    const ULONGLONG deadline = GetTickCount64() + 600;
    Sleep(60);
    while (g_config.requireHiddenCursor && game::CursorHidden() && GetTickCount64() < deadline) Sleep(15);
    Sleep(60);

    TypeUnicode(text::Widen(utf8Text));
    Sleep(40);
    Key(VK_RETURN, true);
    Key(VK_RETURN, false);
    logx::Info("TextHotkey: sent '%s'", utf8Text.c_str());
    return true;
}

bool OnKeyPress(int vk) {
    for (const TextHotkeyEntry& entry : g_config.textHotkeys) {
        if (entry.key != vk) continue;
        if (!g_config.textHotkeyEnabled || entry.text.empty()) return true;

        // Text that starts with the command prefix runs locally (a "command hotkey").
        const std::string prefix = config::Prefix();
        if (text::StartsWith(entry.text, prefix)) {
            commands::Execute(entry.text.substr(prefix.size()));
            return true;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG cooldownMs = static_cast<ULONGLONG>(g_config.textHotkeyCooldown * 1000.0f);
        if (g_lastSent != 0 && now - g_lastSent < cooldownMs) {
            logx::Info("TextHotkey: cooldown, '%s' skipped", entry.text.c_str());
            return true;
        }
        g_lastSent = now;
        SendChatMessage(entry.text);
        return true;
    }
    return false;
}

}  // namespace texthotkey
