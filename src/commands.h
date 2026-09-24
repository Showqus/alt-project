#pragma once

#include <string>

// Chat / control commands. Worker thread only.
namespace commands {

// Runs one command line, without the prefix (e.g. "bind K zoom"). Results go to notify::Send.
void Execute(const std::string& line);

// Handles toggle binds and TextHotkeys for a key pressed in the world. Returns true if used.
bool OnKeyPress(int vk);

}  // namespace commands
