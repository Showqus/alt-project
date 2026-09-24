// Smoke test for BedrockQoL.dll (run under Windows or Wine; under Wine use an X display, e.g.
// xvfb-run, so keyboard layout functions work).
//
// This executable plays the role of Minecraft.Windows.exe: its .text section contains functions
// whose machine code matches the 1.21.5x signatures the DLL scans for (LevelRendererPlayer::getFov,
// Keyboard::feed, the MouseDevice::feed call site, Options::getGamma). It loads the DLL, drives
// those functions like the game would and checks the results.
//
//   fake_game.exe BedrockQoL.dll     run the tests
//   fake_game.exe --host <seconds>   just stay alive (target process for the launcher test)

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
float g_fakeFov = 90.0f;
float g_fakeGamma = 1.0f;
unsigned long long g_fakeCookie = 0x2B992DDFA232ULL;
unsigned char g_keyStates[256];
unsigned g_keyDowns[256];  // number of "down" events the game received per key
unsigned g_feedCount = 0;
unsigned char g_mouseBuffer[64];
volatile int g_lastMouseButton = -1;
volatile int g_lastMouseAction = 0;

float fake_getFov(void* self, float partialTicks, void* a3, void* a4);
void fake_keyboardFeed(int key, int state);
float fake_getGamma(void* options);
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
    test dl, dl
    jz 1f
    lea r8, [rip + g_keyDowns]
    inc dword ptr [r8 + rcx * 4]
1:
    inc dword ptr [rip + g_feedCount]
    add rsp, 0x38
    ret

    .p2align 4, 0xCC
    .globl fake_getGamma
fake_getGamma:
    .byte 0x48, 0x83, 0xEC, 0x28                  # sub rsp, 0x28
    .byte 0x80, 0xB9, 0x20, 0x18, 0x00, 0x00, 0x00 # cmp byte ptr [rcx + 0x1820], 0
    .byte 0x48, 0x8D, 0x54, 0x24, 0x30            # lea rdx, [rsp + 0x30]
    .byte 0x48, 0x8B, 0x01                        # mov rax, [rcx]
    .byte 0x48, 0x8B, 0x40, 0x60                  # mov rax, [rax + 0x60]
    .byte 0x74, 0x38                              # je +0x38
.Lgamma_after_je:
    .byte 0x41, 0xB8, 0x1A, 0x00, 0x00, 0x00      # mov r8d, 0x1A
    movss xmm0, dword ptr [rip + g_fakeGamma]
    add rsp, 0x28
    ret
    .fill (.Lgamma_after_je + 0x38) - ., 1, 0xCC
    movss xmm0, dword ptr [rip + g_fakeGamma]     # je target
    add rsp, 0x28
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

// Options object for fake_getGamma: first qword points to a "vtable" with a readable +0x60 slot,
// byte +0x1820 is non-zero.
alignas(16) unsigned char g_options[0x2000];
void* g_optionsVtable[16];

void Check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

float Fov(float base) {
    g_fakeFov = base;
    return fake_getFov(nullptr, 0.5f, nullptr, nullptr);
}

float Gamma() { return fake_getGamma(g_options); }

bool IsHooked(void* fn) { return *static_cast<unsigned char*>(fn) == 0xE9; }

template <typename F>
bool WaitFor(F condition, DWORD timeoutMs) {
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

std::string IniGet(const char* section, const char* key) {
    char buffer[512] = {};
    const std::string path = [] {
        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        return std::string(temp) + "BedrockQoL\\config.ini";
    }();
    GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), path.c_str());
    return buffer;
}

bool FileContains(const std::wstring& path, const char* needle) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::string content;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) content.append(buffer, n);
    fclose(f);
    return content.find(needle) != std::string::npos;
}

void WriteFile(const std::wstring& path, const char* content) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    std::fputs(content, f);
    std::fclose(f);
}

std::wstring IniGetW(const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(section, key, L"", buffer, 512, (DataDir() + L"\\config.ini").c_str());
    return buffer;
}

void WriteTestConfig() {
    CreateDirectoryW(DataDir().c_str(), nullptr);
    DeleteFileW((DataDir() + L"\\config.ini").c_str());
    if (std::getenv("FAKE_GAME_DEFAULT_CONFIG")) return;  // let the DLL write its own default file
    WriteFile(DataDir() + L"\\config.ini",
              "[General]\nUnloadKey=END\nRequireHiddenCursor=0\n"
              "[Chat]\nCommands=1\nPrefix=.\nOpenKey=T\nCommandKey=SLASH\n"
              "[AutoSprint]\nEnabled=1\nToggleKey=F8\nForwardKey=W\nSprintKey=CTRL\nFallbackSendInput=0\n"
              "[Zoom]\nEnabled=1\nKey=C\nToggle=0\nFactor=4.0 ; inline comment\nMinFactor=1.5\nMaxFactor=50\n"
              "ScrollAdjust=1\nScrollStep=1.25\nRememberScroll=0\nSmooth=0\nZoomHand=0\n"
              "[Fullbright]\nEnabled=0\nToggleKey=NONE\nGamma=25\n"
              "[TextHotkey]\nEnabled=1\nToggleKey=NONE\nCooldown=0\n"
              "[TextHotkeys]\n"
              "[Signatures]\nGetFov=\nKeyboardFeed=\nMouseFeed=\nGetGamma=\n");
}

// config.ini is written first and re-read right after; give the re-read a moment to finish.
bool WaitIni(const char* section, const char* key, const char* value) {
    const bool ok = WaitFor([&] { return IniGet(section, key) == value; }, 2000);
    Sleep(50);
    return ok;
}

void Key(int vk, bool down) { fake_keyboardFeed(vk, down ? 1 : 0); }
void Tap(int vk) {
    Key(vk, true);
    Key(vk, false);
}

// Types ASCII text through Keyboard::feed like a US keyboard would.
void Type(const char* text) {
    for (const char* p = text; *p; ++p) {
        const char c = *p;
        int vk = 0;
        bool shift = false;
        if (c >= 'a' && c <= 'z') vk = c - 'a' + 'A';
        else if (c >= 'A' && c <= 'Z') vk = c, shift = true;
        else if (c >= '0' && c <= '9') vk = c;
        else if (c == ' ') vk = VK_SPACE;
        else if (c == '.') vk = VK_OEM_PERIOD;
        else if (c == ';') vk = VK_OEM_1;
        else if (c == '/') vk = VK_OEM_2;
        else if (c == '-') vk = VK_OEM_MINUS;
        if (shift) Key(VK_SHIFT, true);
        Tap(vk);
        if (shift) Key(VK_SHIFT, false);
    }
}

// Opens the chat with T, types the text and presses Enter. Returns true if Enter reached the game
// (i.e. the message would have been sent to the server).
bool Chat(const char* text) {
    Tap('T');
    Type(text);
    const unsigned enterBefore = g_keyDowns[VK_RETURN];
    Tap(VK_RETURN);
    return g_keyDowns[VK_RETURN] != enterBefore;
}

// Commands run on the DLL's worker thread; give it a moment.
void Settle() { Sleep(150); }

int RunHost(int seconds) {
    std::printf("fake game host running for %d s (pid %lu)\n", seconds, GetCurrentProcessId());
    std::fflush(stdout);
    for (int i = 0; i < seconds * 10; ++i) {
        // Keep the "game functions" busy like a real game would.
        Fov(90.0f);
        Sleep(100);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    g_optionsVtable[12] = nullptr;
    *reinterpret_cast<void**>(g_options) = g_optionsVtable;
    g_options[0x1820] = 1;

    if (argc > 2 && std::strcmp(argv[1], "--host") == 0) return RunHost(std::atoi(argv[2]));

    const char* dll = argc > 1 ? argv[1] : "BedrockQoL.dll";
    WriteTestConfig();
    DeleteFileW((DataDir() + L"\\control\\commands.txt").c_str());

    Check(Near(Fov(90.0f), 90.0f), "getFov works before injection");
    Check(Near(Gamma(), 1.0f), "getGamma works before injection");

    HMODULE module = LoadLibraryA(dll);
    Check(module != nullptr, "DLL loads");
    if (!module) return 1;

    Check(WaitFor([] { return IsHooked((void*)fake_getFov) && IsHooked((void*)fake_keyboardFeed) &&
                              IsHooked((void*)fake_mouseFeed) && IsHooked((void*)fake_getGamma); },
                  5000),
          "getFov, Keyboard::feed, MouseDevice::feed and Options::getGamma found by signature and hooked");
    WaitFor([] { return FileContains(DataDir() + L"\\control\\status.txt", "version="); }, 3000);

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

    // --- AutoSprint ---
    Key('W', true);
    Check(g_keyStates['W'] == 1 && g_keyStates[VK_CONTROL] == 1, "W down -> sprint key fed to the game");
    Key(VK_CONTROL, true);
    Key(VK_CONTROL, false);
    Check(g_keyStates[VK_CONTROL] == 1, "releasing real CTRL while walking keeps sprinting");
    Tap('A');
    Check(g_keyStates[VK_CONTROL] == 1, "other keys do not break sprint");
    Key('W', false);
    Check(g_keyStates['W'] == 0 && g_keyStates[VK_CONTROL] == 0, "W up -> sprint key released");

    Key(VK_F8, true);
    Key(VK_F8, true);  // auto-repeat must not toggle twice
    Key(VK_F8, false);
    Check(WaitIni("AutoSprint", "Enabled", "0"),
          "F8 disables AutoSprint (saved to config.ini)");
    Key('W', true);
    Check(g_keyStates[VK_CONTROL] == 0, "AutoSprint off: W does not sprint");
    Key('W', false);
    Tap(VK_F8);
    Check(WaitIni("AutoSprint", "Enabled", "1"), "F8 enables AutoSprint again");
    Key('W', true);
    Check(g_keyStates[VK_CONTROL] == 1, "AutoSprint on: W sprints");
    Key('W', false);

    // --- Chat commands ---
    Check(Chat("hello world"), "normal chat message is sent (Enter reaches the game)");
    const unsigned escBefore = g_keyDowns[VK_ESCAPE];
    Check(!Chat(".bind k fullbright"), ".bind message is NOT sent to the server");
    Check(g_keyDowns[VK_ESCAPE] == escBefore + 1, "chat is closed with Escape after a command");
    Check(WaitIni("Fullbright", "ToggleKey", "K"), ".bind k fullbright saved");

    Check(Near(Gamma(), 1.0f), "Fullbright off: gamma untouched");
    Tap('K');
    Check(WaitFor([] { return Near(Gamma(), 25.0f); }, 2000), "bound key K turns Fullbright on (gamma 25)");
    Tap('K');
    Check(WaitFor([] { return Near(Gamma(), 1.0f); }, 2000), "K again turns Fullbright off");

    Key('W', true);
    Check(!Chat(".toggle fullbright"), ".toggle is not sent");
    Check(WaitFor([] { return Near(Gamma(), 25.0f); }, 2000), ".toggle fullbright works");
    Key('W', false);

    // Typing in chat must not trigger binds / zoom / autosprint.
    Tap('T');
    Type("kkk c w");
    Check(Near(Fov(90.0f), 90.0f) && g_keyStates[VK_CONTROL] == 0, "keys typed in chat do not trigger features");
    Tap(VK_ESCAPE);
    Settle();
    Check(Near(Gamma(), 25.0f), "K typed in chat did not toggle Fullbright");

    // Editing keys are tracked.
    Tap('T');
    Type(".togglx");
    Tap(VK_BACK);
    Type("e fullbright");
    const unsigned enterBefore = g_keyDowns[VK_RETURN];
    Tap(VK_RETURN);
    Check(g_keyDowns[VK_RETURN] == enterBefore, "Backspace is tracked (.togglx<BS>e -> .toggle)");
    Check(WaitFor([] { return Near(Gamma(), 1.0f); }, 2000), "edited command ran");

    Tap('T');
    Type("some text");
    Key(VK_CONTROL, true);
    Tap('A');
    Key(VK_CONTROL, false);
    Type(".toggle fullbright");  // replaces the selected text
    const unsigned enterBefore2 = g_keyDowns[VK_RETURN];
    Tap(VK_RETURN);
    Check(g_keyDowns[VK_RETURN] == enterBefore2, "Ctrl+A then typing replaces the text");
    Check(WaitFor([] { return Near(Gamma(), 25.0f); }, 2000), "command typed over a selection ran");

    // --- Prefix change ---
    Check(!Chat(".prefix ;"), ".prefix ; is not sent");
    Check(WaitIni("Chat", "Prefix", ";"), "prefix changed to ';'");
    Check(Chat(".toggle fullbright"), "old prefix no longer intercepted");
    Check(!Chat(";toggle fullbright"), ";toggle is intercepted with the new prefix");
    Check(WaitFor([] { return Near(Gamma(), 1.0f); }, 2000), ";toggle fullbright works");

    // --- TextHotkey ---
    Check(!Chat(";th add f6 hello there"), ";th add not sent");
    Check(WaitIni("TextHotkeys", "1", "F6|hello there"), ";th add saved F6|hello there");
    Check(!Chat(";th add f7 ;toggle zoom"), ";th add with a command");
    Check(WaitIni("TextHotkeys", "2", "F7|;toggle zoom"), "command hotkey saved");
    Tap(VK_F7);
    Check(WaitIni("Zoom", "Enabled", "0"), "F7 runs ';toggle zoom' (zoom disabled)");
    Key('C', true);
    Check(Near(Fov(90.0f), 90.0f), "zoom disabled: C does nothing");
    Key('C', false);
    Tap(VK_F7);
    Check(WaitIni("Zoom", "Enabled", "1"), "F7 again enables zoom");
    Check(!Chat(";th remove f6"), ";th remove");
    Check(WaitFor([] { return IniGet("TextHotkeys", "1").empty(); }, 2000), "TextHotkey #1 removed");

    // --- Config profiles ---
    Check(!Chat(";config save test1"), ";config save");
    Check(WaitFor([] { return GetFileAttributesW((DataDir() + L"\\configs\\test1.ini").c_str()) !=
                              INVALID_FILE_ATTRIBUTES; }, 2000),
          "profile test1 saved");
    Check(!Chat(";set zoom.factor 2"), ";set");
    Settle();
    Key('C', true);
    Check(WaitFor([] { return Near(Fov(90.0f), 45.0f); }, 2000), ";set zoom.factor 2 -> 90 -> 45");
    Key('C', false);
    Check(!Chat(";config load test1"), ";config load");
    Settle();
    Key('C', true);
    Check(WaitFor([] { return Near(Fov(90.0f), 22.5f); }, 2000), "profile test1 restored factor 4");
    Key('C', false);
    Check(!Chat(";config export test1"), ";config export");
    Check(WaitFor([] { return GetFileAttributesW((DataDir() + L"\\exports\\test1.ini").c_str()) !=
                              INVALID_FILE_ATTRIBUTES; }, 2000),
          "exported to exports\\test1.ini");

    // --- Commands from the launcher (control\commands.txt) ---
    WriteFile(DataDir() + L"\\control\\commands.txt", "toggle autosprint\n");
    Check(WaitIni("AutoSprint", "Enabled", "0"), "control command toggles AutoSprint");
    Check(FileContains(DataDir() + L"\\control\\notifications.txt", "AutoSprint"), "notifications written for the launcher");
    WriteFile(DataDir() + L"\\control\\commands.txt", "th add f9 \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xF0\x9F\x98\x80\n");
    Check(WaitFor([] { return IniGetW(L"TextHotkeys", L"3") == L"F9|\u043F\u0440\u0438\u0432\u0435\u0442 \U0001F600"; }, 2000),
          "Cyrillic/emoji TextHotkey text survives config.ini");

    // --- Unload through a chat command ---
    Check(!Chat(";unload"), ";unload not sent");
    Check(WaitFor([] { return GetModuleHandleA("BedrockQoL.dll") == nullptr; }, 5000), ";unload unloads the DLL");
    Check(!IsHooked((void*)fake_getFov) && !IsHooked((void*)fake_keyboardFeed) && !IsHooked((void*)fake_getGamma),
          "hooks removed after unload");
    Check(Near(Fov(90.0f), 90.0f) && Near(Gamma(), 1.0f), "game functions work after unload");
    Check(Chat(";toggle zoom"), "after unload chat goes to the game again");

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
