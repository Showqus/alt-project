#include "automation.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../log.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace gui::automation {
namespace {

struct Item {
    std::string label;
    ImGuiID id = 0;
    ImRect rect;
};

std::atomic<bool> g_enabled{false};
std::unordered_map<ImGuiID, ImRect> g_frameRects;  // render thread: every item added this frame
std::vector<Item> g_frameItems;                    // render thread: items with a label
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Item> g_lastFrame;  // under g_lock

bool Matches(const std::string& label, const char* query) {
    if (label == query) return true;
    // "Text##id": match the visible text or the "##id" part.
    const size_t hashes = label.find("##");
    if (hashes == std::string::npos) return false;
    if (label.compare(0, hashes, query) == 0 && std::strlen(query) == hashes) return true;
    return label.compare(hashes, std::string::npos, query) == 0;
}

}  // namespace

void BeginFrame() {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx) ctx->TestEngineHookItems = g_enabled.load(std::memory_order_relaxed);
    g_frameRects.clear();
    g_frameItems.clear();
}

void EndFrame() {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    // Rectangles are resolved at the end of the frame: some widgets (tabs) report their label
    // before they are added.
    for (Item& item : g_frameItems) {
        if (ImGuiWindow* window = ImGui::FindWindowByID(item.id)) {
            item.rect = window->Rect();  // windows register by name
        } else if (auto it = g_frameRects.find(item.id); it != g_frameRects.end()) {
            item.rect = it->second;
        }
    }
    AcquireSRWLockExclusive(&g_lock);
    g_lastFrame.swap(g_frameItems);
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace gui::automation

// --- ImGui test engine hooks (IMGUI_ENABLE_TEST_ENGINE) -------------------------------------

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* data) {
    gui::automation::g_frameRects[id] = data ? data->Rect : bb;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (label && *label) gui::automation::g_frameItems.push_back({label, id, ImRect()});
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

// ImGui assertions (see imgui_config.h): log each failing one once instead of aborting the game.
void BqolImGuiAssert(const char* expr, const char* file, int line) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::set<std::pair<std::string, int>> seen;
    AcquireSRWLockExclusive(&lock);
    const bool first = seen.emplace(file ? file : "", line).second;
    ReleaseSRWLockExclusive(&lock);
    if (first) logx::Error("ImGui assertion failed: %s (%s:%d)", expr, file ? file : "?", line);
}

// Test API: rectangle (x, y, width, height in back buffer pixels) of the widget with this label in the
// last menu frame. Labels: the full ImGui label, its visible text, or its "##id" part.
extern "C" __declspec(dllexport) int BedrockQoL_FindItem(const char* label, float* rect) {
    using namespace gui::automation;
    g_enabled.store(true);
    if (!label) return 0;
    int found = 0;
    AcquireSRWLockShared(&g_lock);
    for (const Item& item : g_lastFrame) {
        if (Matches(item.label, label)) {
            if (rect) {
                rect[0] = item.rect.Min.x;
                rect[1] = item.rect.Min.y;
                rect[2] = item.rect.GetWidth();
                rect[3] = item.rect.GetHeight();
            }
            found = 1;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}
