#include "game.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>

#include "chat.h"
#include "config.h"
#include "gui/state.h"

namespace game {
namespace {

using GetCurrentPackageStringFn = LONG(WINAPI*)(UINT32*, PWSTR);

std::wstring CallPackageApi(const char* name) {
    auto fn = reinterpret_cast<GetCurrentPackageStringFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name)));
    if (!fn) return L"";
    wchar_t buffer[256];
    UINT32 length = 256;
    if (fn(&length, buffer) != ERROR_SUCCESS) return L"";
    return buffer;
}

bool EnsureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool IsOurWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace

std::wstring PackageFullName() { return CallPackageApi("GetCurrentPackageFullName"); }

std::wstring DataDirectory() {
    // Packaged (UWP) game: write into the package's own RoamingState, which the
    // sandboxed game process is allowed to write to.
    const std::wstring family = CallPackageApi("GetCurrentPackageFamilyName");
    if (!family.empty()) {
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
            std::wstring dir = std::wstring(localAppData) + L"\\Packages\\" + family + L"\\RoamingState\\BedrockQoL";
            CoTaskMemFree(localAppData);
            if (EnsureDirectory(dir)) return dir;
        }
    }

    // Fallback: the process temp directory (inside the sandbox this is ...\AC\Temp).
    wchar_t temp[MAX_PATH];
    const DWORD len = GetTempPathW(MAX_PATH, temp);
    if (len > 0 && len < MAX_PATH) {
        std::wstring dir = std::wstring(temp) + L"BedrockQoL";
        if (EnsureDirectory(dir)) return dir;
        return temp;
    }
    return L".";
}

bool CursorHidden() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) return true;  // unknown: don't block features
    return (info.flags & CURSOR_SHOWING) == 0;
}

bool InWorld() {
    if (chat::IsOpen() || gui::MenuOpen()) return false;
    if (!g_config.requireHiddenCursor) return true;
    return CursorHidden();
}

bool InWorldCached() {
    static std::atomic<ULONGLONG> lastCheck{0};
    static std::atomic<bool> lastValue{true};
    const ULONGLONG now = GetTickCount64();
    if (now - lastCheck.load(std::memory_order_relaxed) >= 50) {
        lastValue.store(InWorld(), std::memory_order_relaxed);
        lastCheck.store(now, std::memory_order_relaxed);
    }
    return lastValue.load(std::memory_order_relaxed);
}

bool HasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    if (IsOurWindow(fg)) return true;
    // UWP: the foreground window is ApplicationFrameHost's frame; the game's CoreWindow is its child.
    HWND core = FindWindowExW(fg, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
    return core && IsOurWindow(core);
}

}  // namespace game
