#pragma once

#include <string>

namespace keys {

// Parses a key name from the config ("C", "F8", "CTRL", "END", "0x43", "67", "NONE").
// Returns 0 for NONE / empty, -1 if the name is not recognised.
int Parse(const std::string& name);

// Human readable name for logs.
std::string Name(int vk);

// Characters that key `vk` types with the given modifiers on the current keyboard layout
// (control characters dropped). Returns false for a dead key (the composed character is unknown).
bool Translate(int vk, bool shift, bool ctrl, bool alt, std::wstring& out);

}  // namespace keys
