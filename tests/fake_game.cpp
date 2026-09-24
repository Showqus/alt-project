// Smoke test for BedrockQoL.dll (run under Windows or Wine).
//
// This executable plays the role of Minecraft.Windows.exe: its .text section contains
// functions whose machine code matches the 1.21.5x signatures the DLL scans for
// (LevelRendererPlayer::getFov, Keyboard::feed, the MouseDevice::feed call site).
// It loads the DLL, drives those functions like the game would and checks the results.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

extern "C" {
float g_fakeFov = 90.0f;
unsigned long long g_fakeCookie = 0x2B992DDFA232ULL;
unsigned char g_keyStates[256];
unsigned g_feedCount = 0;
unsigned char g_mouseBuffer[64];
volatile int g_lastMouseButton = -1;
volatile int g_lastMouseAction = 0;

float fake_getFov(void* self, float partialTicks, void* a3, void* a4);
void fake_keyboardFeed(int key, int state);
void fake_mouseCaller();

__attribute__((noinline)) void fake_mouseFeed(void* device, char button, char action, short x, short y, short dx,
                                              short dy, char a8) {
    g_lastMouseButton = button;
    g_lastMouseAction = action;
    g_mouseBuffer[0] = static_cast<unsigned char>(x + y + dx + dy + a8);
    g_mouseBuffer[1] = device != nullptr;
}
}

// Byte-for-byte shapes that satisfy the signatures in src/hooks.cpp.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4, 0xCC
    .globl fake_getFov
fake_getFov:
    mov rax, rsp                                  # 48 8B C4        \
    mov qword ptr [rax + 0x08], rbx               # 48 89 58 08     / 7 wildcard bytes
    mov qword ptr [rax + 0x10], rsi               # 48 89 ? ?
    push rdi                                      # 57
    sub rsp, 0x90                                 # 48 81 EC ? ? ? ?
    movaps xmmword ptr [rax - 0x18], xmm6         # 0F 29 ? ?
    movaps xmmword ptr [rax - 0x28], xmm7         # 0F 29 ? ?
    movaps xmmword ptr [rax - 0x38], xmm8         # 44 0F ? ? ?
    movaps xmmword ptr [rax - 0x48], xmm9         # 44 0F ? ? ?
    mov rax, qword ptr [rip + g_fakeCookie]       # 48 8B ? ? ? ? ?
    .byte 0x48, 0x33, 0xC4                        # xor rax, rsp (48 33 ?)
    mov qword ptr [rsp + 0x40], rax               # 48 89 ? ? ?
    movaps xmm0, xmm9                             # 41 0F
    movss xmm0, dword ptr [rip + g_fakeFov]
    movaps xmm6, xmmword ptr [rsp + 0x80]
    movaps xmm7, xmmword ptr [rsp + 0x70]
    movaps xmm8, xmmword ptr [rsp + 0x60]
    movaps xmm9, xmmword ptr [rsp + 0x50]
    add rsp, 0x90
    pop rdi
    mov rbx, qword ptr [rsp + 0x08]
    mov rsi, qword ptr [rsp + 0x10]
    ret

    .p2align 4, 0xCC
    .globl fake_keyboardFeed
fake_keyboardFeed:
    sub rsp, 0x38                                 # 48 83 EC 38     \
    movzx ecx, cl                                 # 0F B6 C9        / 7 wildcard bytes
    lea r8, [rip + g_keyStates]                   # 4C 8D 05 ? ? ? ?
    mov dword ptr [rsp + 0x20], edx               # 89 54 24 20
    mov byte ptr [rsp + 0x28], dl                 # 88 ...
    mov byte ptr [r8 + rcx], dl
    inc dword ptr [rip + g_feedCount]
    add rsp, 0x38
    ret

    .p2align 4, 0xCC
    .globl fake_mouseCaller
fake_mouseCaller:
    push rbp
    push rdi
    push rbx
    sub rsp, 0x40
    lea rdi, [rip + g_mouseBuffer]
    xor ebx, ebx
    xor ebp, ebp
    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    mov qword ptr [rsp + 0x20], 0
    mov qword ptr [rsp + 0x28], 0
    mov qword ptr [rsp + 0x30], 0
    mov qword ptr [rsp + 0x38], 0
    call fake_mouseFeed                           # E8 ? ? ? ?
    mov byte ptr [rdi + rbx + 0x10], bpl          # 40 88 6C 1F 10
    add rsp, 0x40
    pop rbx
    pop rdi
    pop rbp
    ret
    .att_syntax prefix
)");

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

float Fov(float base) {
    g_fakeFov = base;
    return fake_getFov(nullptr, 0.5f, nullptr, nullptr);
}

bool IsHooked(void* fn) { return *static_cast<unsigned char*>(fn) == 0xE9; }

bool WaitFor(bool (*condition)(), DWORD timeoutMs) {
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < end) {
        if (condition()) return true;
        Sleep(10);
    }
    return condition();
}

std::wstring DataDir() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp) + L"BedrockQoL";
}

void WriteTestConfig() {
    CreateDirectoryW(DataDir().c_str(), nullptr);
    const std::wstring path = DataDir() + L"\\config.ini";
    FILE* f = _wfopen(path.c_str(), L"wb");
    std::fputs(
        "[General]\nUnloadKey=END\nRequireHiddenCursor=0\n"
        "[AutoSprint]\nEnabled=1\nToggleKey=F8\nForwardKey=W\nSprintKey=CTRL\nFallbackSendInput=0\n"
        "[Zoom]\nEnabled=1\nKey=C\nToggle=0\nFactor=4.0 ; inline comment\nMinFactor=1.5\nMaxFactor=50\n"
        "ScrollAdjust=1\nScrollStep=1.25\nRememberScroll=0\nSmooth=0\nZoomHand=0\n"
        "[Signatures]\nGetFov=\nKeyboardFeed=\nMouseFeed=\n",
        f);
    std::fclose(f);
}

void Key(int vk, bool down) { fake_keyboardFeed(vk, down ? 1 : 0); }

}  // namespace

int main(int argc, char** argv) {
    const char* dll = argc > 1 ? argv[1] : "BedrockQoL.dll";
    WriteTestConfig();

    Check(Near(Fov(90.0f), 90.0f), "getFov works before injection");

    HMODULE module = LoadLibraryA(dll);
    Check(module != nullptr, "DLL loads");
    if (!module) return 1;

    Check(WaitFor([] { return IsHooked((void*)fake_getFov) && IsHooked((void*)fake_keyboardFeed) &&
                              IsHooked((void*)fake_mouseFeed); },
                  5000),
          "getFov, Keyboard::feed and MouseDevice::feed found by signature and hooked");

    // --- Zoom ---
    Check(Near(Fov(90.0f), 90.0f), "FOV untouched while not zooming");
    Key('C', true);
    Check(Near(Fov(90.0f), 22.5f), "holding C zooms 4x (90 -> 22.5)");
    Check(Near(Fov(70.0f), 70.0f), "hand FOV (70) is left alone");
    Check(Near(Fov(90.0f), 22.5f), "world FOV still zoomed");

    g_lastMouseButton = -1;
    fake_mouseFeed(nullptr, 4, static_cast<char>(0x78), 0, 0, 0, 0, 0);
    Check(g_lastMouseButton == -1, "scroll is swallowed while zooming (hotbar does not move)");
    Check(Near(Fov(90.0f), 18.0f), "scroll up increases zoom (x4 -> x5)");
    fake_mouseFeed(nullptr, 4, static_cast<char>(0x88), 0, 0, 0, 0, 0);
    fake_mouseFeed(nullptr, 4, static_cast<char>(0x88), 0, 0, 0, 0, 0);
    Check(Near(Fov(90.0f), 28.125f), "scroll down decreases zoom (x5 -> x3.2)");

    Key('C', false);
    Check(Near(Fov(90.0f), 90.0f), "releasing C restores FOV");
    fake_mouseFeed(nullptr, 4, static_cast<char>(0x78), 0, 0, 0, 0, 0);
    Check(g_lastMouseButton == 4, "scroll passes through when not zooming");
    Key('C', true);
    Check(Near(Fov(90.0f), 22.5f), "zoom factor resets to config value on next zoom");
    Key('C', false);

    // --- AutoSprint ---
    Key('W', true);
    Check(g_keyStates['W'] == 1 && g_keyStates[VK_CONTROL] == 1, "W down -> sprint key fed to the game");
    Key(VK_CONTROL, true);
    Key(VK_CONTROL, false);
    Check(g_keyStates[VK_CONTROL] == 1, "releasing real CTRL while walking keeps sprinting");
    Key('A', true);
    Key('A', false);
    Check(g_keyStates[VK_CONTROL] == 1, "other keys do not break sprint");
    Key('W', false);
    Check(g_keyStates['W'] == 0 && g_keyStates[VK_CONTROL] == 0, "W up -> sprint key released");

    Key(VK_F8, true);
    Key(VK_F8, true);  // auto-repeat must not toggle twice
    Key(VK_F8, false);
    Key('W', true);
    Check(g_keyStates[VK_CONTROL] == 0, "F8 disables AutoSprint");
    Key('W', false);
    Key(VK_F8, true);
    Key(VK_F8, false);
    Key('W', true);
    Check(g_keyStates[VK_CONTROL] == 1, "F8 enables AutoSprint again");

    // --- Unload ---
    Key(VK_END, true);
    Check(g_keyStates[VK_CONTROL] == 0, "END releases the injected sprint key");
    Key(VK_END, false);
    Key('W', false);
    Check(WaitFor([] { return GetModuleHandleA("BedrockQoL.dll") == nullptr; }, 5000), "END unloads the DLL");
    Check(!IsHooked((void*)fake_getFov) && !IsHooked((void*)fake_keyboardFeed), "hooks removed after unload");
    Check(Near(Fov(90.0f), 90.0f), "getFov works after unload");

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
