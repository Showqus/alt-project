#pragma once

#include <string>

// TextHotkey: a key sends a chat message (or runs a command if the text starts with the prefix).
// Worker thread only.
namespace texthotkey {

// Returns true if the key belonged to a TextHotkey entry.
bool OnKeyPress(int vk);

// Types `utf8Text` into the game chat and presses Enter (via SendInput, so it works with any
// game version). Returns false if the game does not have focus.
bool SendChatMessage(const std::string& utf8Text);

}  // namespace texthotkey
