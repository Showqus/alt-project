#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

// Hand-off from game threads (hooks) to the worker thread, where anything slow or anything
// that touches config/strings happens.
namespace events {

enum class Type {
    KeyPress,  // a key was pressed while in the world (binds, TextHotkey)
    IgnoredKey,  // a key was pressed outside the world (cursor visible): bound keys tell why
    Command,   // a chat/control command, without prefix
    ConfigWrite,  // settings changed in the menu
};

struct Event {
    Type type = Type::KeyPress;
    int vk = 0;
    std::string text;
    std::vector<ConfigEntry> entries;  // ConfigWrite
    uint64_t writeId = 0;              // ConfigWrite: reported back through gui::Snapshot::appliedWrite
};

void Push(const Event& event);
bool Pop(Event& out);

}  // namespace events
