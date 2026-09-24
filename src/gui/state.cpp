#include "state.h"

#include <atomic>
#include <deque>

#include "../events.h"

namespace gui {
namespace {

SRWLOCK g_snapshotLock = SRWLOCK_INIT;
std::shared_ptr<const Snapshot> g_snapshot = std::make_shared<Snapshot>();
std::atomic<uint64_t> g_appliedWrite{0};

std::atomic<bool> g_menuOpen{false};
std::atomic<ULONGLONG> g_hintsUntil{0};  // GetTickCount64 until which .help keeps the command list up

SRWLOCK g_toastLock = SRWLOCK_INIT;
std::deque<Toast> g_toasts;

// Render thread only.
std::vector<ConfigEntry> g_pendingWrites;
std::atomic<uint64_t> g_lastWriteId{0};

}  // namespace

void OnConfigReloaded() {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->theme = ThemeFromIni(config::ReadSection("Theme"), config::ReadSection("Menu"));
    snapshot->textHotkeys = g_config.textHotkeys;
    snapshot->appliedWrite = g_appliedWrite.load();

    AcquireSRWLockExclusive(&g_snapshotLock);
    snapshot->version = g_snapshot->version + 1;
    g_snapshot = snapshot;
    ReleaseSRWLockExclusive(&g_snapshotLock);
}

void SetAppliedWrite(uint64_t id) { g_appliedWrite.store(id); }

std::shared_ptr<const Snapshot> Latest() {
    AcquireSRWLockShared(&g_snapshotLock);
    std::shared_ptr<const Snapshot> copy = g_snapshot;
    ReleaseSRWLockShared(&g_snapshotLock);
    return copy;
}

void ShowCommandHints(ULONGLONG milliseconds) { g_hintsUntil.store(GetTickCount64() + milliseconds); }

bool CommandHintsRequested() { return GetTickCount64() < g_hintsUntil.load(std::memory_order_relaxed); }

bool MenuOpen() { return g_menuOpen.load(std::memory_order_relaxed); }

void SetMenuOpen(bool open) { g_menuOpen.store(open); }

void PushToast(const std::string& utf8) {
    AcquireSRWLockExclusive(&g_toastLock);
    g_toasts.push_back(Toast{utf8, GetTickCount64()});
    while (g_toasts.size() > 8) g_toasts.pop_front();
    ReleaseSRWLockExclusive(&g_toastLock);
}

void TakeToasts(std::vector<Toast>& out) {
    AcquireSRWLockExclusive(&g_toastLock);
    for (Toast& t : g_toasts) out.push_back(std::move(t));
    g_toasts.clear();
    ReleaseSRWLockExclusive(&g_toastLock);
}

void QueueWrite(const ConfigEntry& entry) {
    // A later change of the same key replaces the queued one.
    for (ConfigEntry& e : g_pendingWrites) {
        if (e.op != ConfigEntry::Op::RemoveSection && entry.op != ConfigEntry::Op::RemoveSection &&
            e.section == entry.section && e.key == entry.key) {
            e = entry;
            return;
        }
    }
    g_pendingWrites.push_back(entry);
}

void QueueWrites(const std::vector<ConfigEntry>& entries) {
    for (const ConfigEntry& e : entries) g_pendingWrites.push_back(e);
}

void QueueCommand(const std::string& line) {
    FlushWrites();  // keep the order: settings first, then the command
    events::Event event;
    event.type = events::Type::Command;
    event.text = line;
    events::Push(event);
}

void FlushWrites() {
    if (g_pendingWrites.empty()) return;
    events::Event event;
    event.type = events::Type::ConfigWrite;
    event.entries.swap(g_pendingWrites);
    event.writeId = g_lastWriteId.load() + 1;
    g_lastWriteId.store(event.writeId);
    events::Push(event);
}

uint64_t LastWriteId() { return g_lastWriteId.load(); }

}  // namespace gui
