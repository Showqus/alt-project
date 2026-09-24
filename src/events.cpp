#include "events.h"

#include <windows.h>

#include <deque>

namespace events {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
std::deque<Event> g_queue;

}  // namespace

void Push(const Event& event) {
    AcquireSRWLockExclusive(&g_lock);
    // Key presses may be dropped when the worker falls behind; commands and settings never are.
    if (g_queue.size() < 256 || event.type != Type::KeyPress) g_queue.push_back(event);
    ReleaseSRWLockExclusive(&g_lock);
}

bool Pop(Event& out) {
    AcquireSRWLockExclusive(&g_lock);
    const bool any = !g_queue.empty();
    if (any) {
        out = std::move(g_queue.front());
        g_queue.pop_front();
    }
    ReleaseSRWLockExclusive(&g_lock);
    return any;
}

}  // namespace events
