#pragma once

#include <string>
#include <vector>

// Chat / control commands. Worker thread only.
namespace commands {

// Runs one command line, without the prefix (e.g. "bind K zoom"). Results go to notify::Send.
void Execute(const std::string& line);

// Handles toggle binds and TextHotkeys for a key pressed in the world. Returns true if used.
bool OnKeyPress(int vk);

// A key was pressed outside the world (the cursor is visible): if it is bound, tells the player once
// per session why nothing happened.
void OnIgnoredKey(int vk);

// The chat commands, for .help and the "Доступные команды" list next to the chat. Any thread.
struct CommandInfo {
    std::string usage;               // without the prefix: "bind <клавиша> <функция>"
    std::string description;         // "назначить клавишу функции"
    std::vector<std::string> names;  // the command word and its aliases
    bool takesFunction = false;      // the list also shows the function names
};
const std::vector<CommandInfo>& CommandList();

// "autosprint, zoom, fullbright, texthotkey, unload, menu"
const std::string& FunctionNames();

}  // namespace commands
