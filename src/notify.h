#pragma once

#include <string>

// User-facing messages (command results, toggles). Written to the log, shown in the game by the
// menu overlay, and written to control\notifications.txt, which the launcher shows as Windows
// notifications.
namespace notify {

void Init(const std::wstring& dataDirectory);
// `toast` = false: not shown in the game (it shows the text in another way, e.g. the command list).
void Send(const std::string& utf8Message, bool toast = true);

}  // namespace notify
