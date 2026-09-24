#pragma once

#include <string>

// Hand-off from game threads (hooks) to the worker thread, where anything slow or anything
// that touches config/strings happens.
namespace events {

enum class Type {
    KeyPress,  // a key was pressed while in the world (binds, TextHotkey)
    Command,   // a chat/control command, without prefix
};

struct Event {
    Type type = Type::KeyPress;
    int vk = 0;
    std::string text;
};

void Push(const Event& event);
bool Pop(Event& out);

}  // namespace events
