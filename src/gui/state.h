#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "theme.h"

// State shared between the worker thread (config, commands), the game's input thread (hooks) and
// the render thread (the menu).
namespace gui {

// What the menu shows beyond the lock-free scalars of g_config. Published after every config reload.
struct Snapshot {
    uint64_t version = 0;
    uint64_t appliedWrite = 0;  // last menu write batch (QueueWrite) already contained in this snapshot
    Theme theme;
    std::vector<TextHotkeyEntry> textHotkeys;
};

// Worker thread: called at the end of config::Reload.
void OnConfigReloaded();

// Worker thread: the next published snapshot reports write batch `id` as applied.
void SetAppliedWrite(uint64_t id);

// Any thread.
std::shared_ptr<const Snapshot> Latest();

// Menu visibility. Opened on the input thread (menu key) or by the .menu command, closed by the
// menu itself.
bool MenuOpen();
void SetMenuOpen(bool open);

// In-game notifications (every notify::Send). Any thread.
struct Toast {
    std::string text;
    ULONGLONG createdAt = 0;
};
void PushToast(const std::string& utf8);
void TakeToasts(std::vector<Toast>& out);  // render thread: moves the queued toasts into `out`

// Render thread: settings changed in the menu are written by the worker thread in batches.
void QueueWrite(const ConfigEntry& entry);
void QueueWrites(const std::vector<ConfigEntry>& entries);
void QueueCommand(const std::string& line);  // runs like a chat command (without prefix)
void FlushWrites();                          // posts the queued batch, if any
uint64_t LastWriteId();                      // id of the last posted batch (0 = none)

}  // namespace gui
