#pragma once

#include <string>

namespace keys {

// Parses a key name from the config ("C", "F8", "CTRL", "END", "0x43", "67", "NONE").
// Returns 0 for NONE / empty, -1 if the name is not recognised.
int Parse(const std::string& name);

// Human readable name for logs.
std::string Name(int vk);

}  // namespace keys
