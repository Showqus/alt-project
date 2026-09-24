// Smoke test for BedrockQoL.dll (run under Windows or Wine; under Wine use an X display, e.g.
// xvfb-run, so keyboard layout functions and Direct3D work).
//
// This executable plays the role of Minecraft.Windows.exe: its .text section contains functions
// whose machine code matches the 1.21.5x signatures the DLL scans for (LevelRendererPlayer::getFov,
// Keyboard::feed, the MouseDevice::feed call site, Options::getGamma), and a render thread presents
// frames through a Direct3D 11 (or 12) swap chain like the game does, so the in-game menu is drawn
// into them. It loads the DLL, drives those functions like the game would and checks the results,
// including the pixels of the presented frames.
//
//   fake_game.exe BedrockQoL.dll            run the tests (Direct3D 11)
//   fake_game.exe BedrockQoL.dll --d3d12    run the tests with a Direct3D 12 swap chain
//   fake_game.exe --host <seconds>          just stay alive (target process for the launcher test)

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// --- Render thread ---------------------------------------------------------------------------
// Like the game's render thread: clears the back buffer and presents about 100 times per second, so
// the DLL's IDXGISwapChain::Present hook draws the menu into the frame. Capture() reads back the last
// presented frame (both swap chains keep their contents after Present).

constexpr UINT kWidth = 800;
constexpr UINT kHeight = 600;
const float kClear[4] = {0.10f, 0.20f, 0.30f, 1.0f};  // RGB 26, 51, 77

bool g_useD3D12 = false;
std::atomic<bool> g_renderStop{false};
std::atomic<bool> g_renderPause{false};  // see main()
std::atomic<uint32_t> g_resizeTo{0};       // (width << 16) | height: ResizeBuffers on the render thread
std::atomic<long> g_resizeResult{0};
std::atomic<bool> g_renderPaused{false};
std::atomic<int> g_rendererState{0};  // 0 = starting, 1 = presenting, -1 = failed
std::atomic<unsigned> g_frames{0};
std::atomic<bool> g_captureWanted{false};
HANDLE g_captureDone = nullptr;
std::vector<uint32_t> g_captured;  // RGBA8, kWidth x kHeight
void* g_presentFn = nullptr;       // IDXGISwapChain::Present used by the "game"
HANDLE g_renderThread = nullptr;

// Like the real game: an arrow cursor on screens, a null cursor (hidden, but Windows still reports it
// as "showing") while the player is in the world. Set by the render thread, which owns the window.
std::atomic<bool> g_cursorInWorld{false};
HCURSOR g_arrow = nullptr;

void PumpMessages() {
    SetCursor(g_cursorInWorld ? nullptr : g_arrow);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void StoreCapture(const uint8_t* data, UINT rowPitch) {
    g_captured.resize(static_cast<size_t>(kWidth) * kHeight);
    for (UINT y = 0; y < kHeight; ++y) std::memcpy(&g_captured[y * kWidth], data + y * rowPitch, kWidth * 4);
}

void RunD3D11(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = kWidth;
    sd.BufferDesc.Height = kHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;  // the back buffer keeps the presented frame
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &sd, &chain, &device, nullptr, &context))) {
        g_rendererState = -1;
        return;
    }
    g_presentFn = (*reinterpret_cast<void***>(chain))[8];
    ID3D11Texture2D* buffer = nullptr;
    chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
    ID3D11RenderTargetView* view = nullptr;
    device->CreateRenderTargetView(buffer, nullptr, &view);
    D3D11_TEXTURE2D_DESC td{};
    buffer->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    device->CreateTexture2D(&td, nullptr, &staging);

    g_rendererState = 1;
    while (!g_renderStop) {
        PumpMessages();
        g_renderPaused = g_renderPause.load();
        if (g_renderPaused) {
            Sleep(5);
            continue;
        }
        if (const uint32_t size = g_resizeTo.exchange(0)) {
            // Like the game when its window changes size: drop the views, resize, recreate.
            view->Release();
            buffer->Release();
            const HRESULT hr = chain->ResizeBuffers(0, size >> 16, size & 0xFFFF, DXGI_FORMAT_UNKNOWN, 0);
            chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
            device->CreateRenderTargetView(buffer, nullptr, &view);
            g_resizeResult = hr;
        }
        context->ClearRenderTargetView(view, kClear);
        chain->Present(0, 0);
        ++g_frames;
        D3D11_TEXTURE2D_DESC current{};
        buffer->GetDesc(&current);
        if (g_captureWanted && current.Width == kWidth && current.Height == kHeight) {
            context->CopyResource(staging, buffer);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                StoreCapture(static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch);
                context->Unmap(staging, 0);
            }
            g_captureWanted = false;
            SetEvent(g_captureDone);
        }
        Sleep(8);
    }
    staging->Release();
    view->Release();
    buffer->Release();
    context->Release();
    chain->Release();
    device->Release();
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from,
                D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    list->ResourceBarrier(1, &b);
}

void RunD3D12(HWND hwnd) {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    IDXGISwapChain1* chain1 = nullptr;
    IDXGISwapChain3* chain = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = kWidth;
    sd.Height = kHeight;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                 reinterpret_cast<void**>(&device))) ||
        FAILED(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue))) ||
        FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory))) ||
        FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &chain1)) ||
        FAILED(chain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain)))) {
        g_rendererState = -1;
        return;
    }
    g_presentFn = (*reinterpret_cast<void***>(chain))[8];

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 2;
    ID3D12DescriptorHeap* heap = nullptr;
    device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&heap));
    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource* buffers[2] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[2];
    for (UINT i = 0; i < 2; ++i) {
        chain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&buffers[i]));
        rtv[i] = heap->GetCPUDescriptorHandleForHeapStart();
        rtv[i].ptr += i * rtvSize;
        device->CreateRenderTargetView(buffers[i], nullptr, rtv[i]);
    }
    ID3D12CommandAllocator* allocator = nullptr;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                   reinterpret_cast<void**>(&allocator));
    ID3D12GraphicsCommandList* list = nullptr;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
                              reinterpret_cast<void**>(&list));
    list->Close();
    ID3D12Fence* fence = nullptr;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence));
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fenceValue = 0;
    auto waitGpu = [&] {
        queue->Signal(fence, ++fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, 5000);
        }
    };

    // Read-back buffer for Capture().
    const D3D12_RESOURCE_DESC bufferDesc = buffers[0]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total = 0;
    device->GetCopyableFootprints(&bufferDesc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = total;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* readback = nullptr;
    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    __uuidof(ID3D12Resource), reinterpret_cast<void**>(&readback));

    g_rendererState = 1;
    while (!g_renderStop) {
        g_renderPaused = g_renderPause.load();
        if (g_renderPaused) {
            Sleep(5);
            continue;
        }
        PumpMessages();
        if (const uint32_t size = g_resizeTo.exchange(0)) {
            waitGpu();
            for (ID3D12Resource*& b : buffers) {
                b->Release();
                b = nullptr;
            }
            const HRESULT hr = chain->ResizeBuffers(2, size >> 16, size & 0xFFFF, DXGI_FORMAT_UNKNOWN, 0);
            for (UINT i = 0; i < 2; ++i) {
                chain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&buffers[i]));
                device->CreateRenderTargetView(buffers[i], nullptr, rtv[i]);
            }
            g_resizeResult = hr;
        }
        const UINT index = chain->GetCurrentBackBufferIndex();
        allocator->Reset();
        list->Reset(allocator, nullptr);
        Transition(list, buffers[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(rtv[index], kClear, 0, nullptr);
        Transition(list, buffers[index], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        chain->Present(0, 0);
        waitGpu();
        ++g_frames;
        if (g_captureWanted && buffers[index]->GetDesc().Width == kWidth && buffers[index]->GetDesc().Height == kHeight) {
            allocator->Reset();
            list->Reset(allocator, nullptr);
            Transition(list, buffers[index], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = buffers[index];
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            Transition(list, buffers[index], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
            list->Close();
            queue->ExecuteCommandLists(1, lists);
            waitGpu();
            void* data = nullptr;
            D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
            if (SUCCEEDED(readback->Map(0, &range, &data))) {
                StoreCapture(static_cast<const uint8_t*>(data), footprint.Footprint.RowPitch);
                D3D12_RANGE none{0, 0};
                readback->Unmap(0, &none);
            }
            g_captureWanted = false;
            SetEvent(g_captureDone);
        }
        Sleep(8);
    }
    waitGpu();
    CloseHandle(fenceEvent);
    readback->Release();
    fence->Release();
    list->Release();
    allocator->Release();
    for (ID3D12Resource* b : buffers) b->Release();
    heap->Release();
    chain->Release();
    chain1->Release();
    factory->Release();
    queue->Release();
    device->Release();
}

DWORD WINAPI RenderThread(LPVOID) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FakeMinecraft";
    g_arrow = LoadCursorA(nullptr, IDC_ARROW);
    wc.hCursor = g_arrow;
    RegisterClassW(&wc);
    RECT r{0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"FakeMinecraft", L"Fake Minecraft", WS_OVERLAPPEDWINDOW, 0, 0, r.right - r.left,
                              r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        g_rendererState = -1;
        return 0;
    }
    // Foreground like a game the player looks at: GetCursorInfo reports the foreground thread's cursor.
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    if (g_useD3D12) {
        RunD3D12(hwnd);
    } else {
        RunD3D11(hwnd);
    }
    DestroyWindow(hwnd);
    return 0;
}

bool StartRenderer() {
    g_captureDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_renderThread = CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr);
    const ULONGLONG end = GetTickCount64() + 20000;
    while (g_rendererState == 0 && GetTickCount64() < end) Sleep(10);
    return g_rendererState == 1;
}

void StopRenderer() {
    g_renderStop = true;
    if (g_renderThread) {
        WaitForSingleObject(g_renderThread, 5000);
        CloseHandle(g_renderThread);
        g_renderThread = nullptr;
    }
}

// Resizes the swap chain buffers on the render thread; returns the HRESULT of ResizeBuffers.
long Resize(UINT width, UINT height) {
    g_resizeResult = 1;
    g_resizeTo = (width << 16) | height;
    const ULONGLONG end = GetTickCount64() + 5000;
    while (g_resizeResult == 1 && GetTickCount64() < end) Sleep(5);
    return g_resizeResult;
}

// The last presented frame.
std::vector<uint32_t> Capture() {
    if (g_rendererState != 1) return {};
    ResetEvent(g_captureDone);
    g_captureWanted = true;
    if (WaitForSingleObject(g_captureDone, 5000) != WAIT_OBJECT_0) return {};
    return g_captured;
}

bool PixelNear(const std::vector<uint32_t>& frame, int x, int y, int r, int g, int b, int tolerance = 6) {
    if (frame.empty()) return false;
    const uint32_t p = frame[static_cast<size_t>(y) * kWidth + x];
    const int pr = p & 0xFF, pg = (p >> 8) & 0xFF, pb = (p >> 16) & 0xFF;
    return std::abs(pr - r) <= tolerance && std::abs(pg - g) <= tolerance && std::abs(pb - b) <= tolerance;
}

// FAKE_GAME_SCREENSHOTS=<folder>: saves the presented frame as <folder>\<name>.bmp (for looking at the menu).
void Screenshot(const char* name) {
    const char* folder = std::getenv("FAKE_GAME_SCREENSHOTS");
    if (!folder) return;
    Sleep(250);  // let fades and layout settle
    const std::vector<uint32_t> frame = Capture();
    if (frame.empty()) return;
    const std::string path = std::string(folder) + "\\" + name + ".bmp";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    unsigned char header[54] = {'B', 'M'};
    auto put32 = [&](int offset, uint32_t v) { std::memcpy(header + offset, &v, 4); };
    put32(2, 54 + kWidth * kHeight * 4);
    put32(10, 54);
    put32(14, 40);
    put32(18, kWidth);
    put32(22, static_cast<uint32_t>(-static_cast<int32_t>(kHeight)));  // top-down
    header[26] = 1;
    header[28] = 32;
    std::fwrite(header, 1, sizeof(header), f);
    for (uint32_t p : frame) {
        const unsigned char bgra[4] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8),
                                       static_cast<unsigned char>(p), 255};
        std::fwrite(bgra, 1, 4, f);
    }
    std::fclose(f);
}

// The game's own clear color (nothing drawn over it).
bool IsClear(const std::vector<uint32_t>& frame, int x, int y) { return PixelNear(frame, x, y, 26, 51, 77, 3); }

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
              "[Menu]\nStartDelay=0\n"
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


// --- In-game menu ------------------------------------------------------------------------------

using FindItemFn = int (*)(const char* label, float* rect);
FindItemFn g_findItem = nullptr;

bool FindItem(const char* label, float* rect = nullptr) {
    float r[4];
    return g_findItem && g_findItem(label, rect ? rect : r) != 0;
}

void MouseMove(int x, int y) { fake_mouseFeed(nullptr, 0, 0, static_cast<short>(x), static_cast<short>(y), 0, 0, 0); }

void Click(int x, int y) {
    MouseMove(x, y);
    Sleep(40);
    fake_mouseFeed(nullptr, 1, 1, static_cast<short>(x), static_cast<short>(y), 0, 0, 0);
    Sleep(60);
    fake_mouseFeed(nullptr, 1, 0, static_cast<short>(x), static_cast<short>(y), 0, 0, 0);
}

// Clicks the middle of a widget of the menu (found by its ImGui label / "##id"), once its position
// is stable (a window that just appeared or changed page settles within a few frames).
bool StableItem(const char* label, float* r) {
    float previous[4] = {-1, -1, -1, -1};
    return WaitFor([&] {
        if (!FindItem(label, r) || r[2] <= 0 || r[3] <= 0) return false;
        const bool stable = std::memcmp(r, previous, sizeof(previous)) == 0;
        std::memcpy(previous, r, sizeof(previous));
        if (!stable) Sleep(60);
        return stable;
    }, 3000);
}

bool ClickItem(const char* label) {
    float r[4] = {};
    if (!StableItem(label, r)) return false;
    if (std::getenv("FAKE_GAME_VERBOSE")) std::printf("  click %s at %.0f,%.0f %.0fx%.0f\n", label, r[0], r[1], r[2], r[3]);
    Click(static_cast<int>(r[0] + r[2] * 0.5f), static_cast<int>(r[1] + r[3] * 0.5f));
    Sleep(80);
    return true;
}

// Runs control commands (like BedrockQoLLauncher.exe --cmd) and waits until the DLL took the file.
void Commands(const char* lines) {
    const std::wstring path = DataDir() + L"\\control\\commands.txt";
    WaitFor([&] { return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES; }, 2000);
    WriteFile(path, lines);
    WaitFor([&] { return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES; }, 2000);
    Sleep(100);
}

std::wstring IniGetFileW(const std::wstring& file, const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[512] = {};
    GetPrivateProfileStringW(section, key, L"", buffer, 512, file.c_str());
    return buffer;
}

// 4x4 24-bit BMP of one color.
void WriteBmp(const std::wstring& path, unsigned char r, unsigned char g, unsigned char b) {
    unsigned char file[54 + 48] = {'B', 'M'};
    auto put32 = [&](int offset, uint32_t v) { std::memcpy(file + offset, &v, 4); };
    put32(2, sizeof(file));
    put32(10, 54);
    put32(14, 40);
    put32(18, 4);
    put32(22, 4);
    file[26] = 1;
    file[28] = 24;
    for (int i = 0; i < 16; ++i) {
        file[54 + i * 3] = b;
        file[54 + i * 3 + 1] = g;
        file[54 + i * 3 + 2] = r;
    }
    FILE* f = _wfopen(path.c_str(), L"wb");
    fwrite(file, 1, sizeof(file), f);
    fclose(f);
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir = path;
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

bool Notified(const char* text) {
    return WaitFor([&] { return FileContains(DataDir() + L"\\control\\notifications.txt", text); }, 3000);
}

// "Доступные команды" next to the chat, feedback for keys that do nothing, .status.
void CommandListTests() {
    // The prefix is ';' here (changed by the chat tests).
    float all[4] = {}, some[4] = {};
    Tap('T');
    Type(";");
    Check(StableItem("###commands", all), "typing the command prefix in the chat shows 'Доступные команды'");
    if (std::getenv("FAKE_GAME_VERBOSE")) std::printf("  command list at %.0f,%.0f %.0fx%.0f\n", all[0], all[1], all[2], all[3]);
    Check(all[0] < 40 && all[1] + all[3] > kHeight * 0.7f && all[3] > 200,
          "the list sits bottom left, above the chat input, with every command");
    Screenshot("8-commands");
    Type("bi");
    Check(WaitFor([&] { return StableItem("###commands", some) && some[3] < all[3] * 0.5f; }, 3000),
          "typing ';bi' narrows the list to the matching commands");
    Tap(VK_ESCAPE);
    Check(WaitFor([] { return !FindItem("###commands"); }, 2000), "closing the chat hides the list");
    Tap('T');
    Type("hello");
    Sleep(300);
    Check(!FindItem("###commands"), "normal chat text shows no list");
    Tap(VK_ESCAPE);
    Tap('T');
    Type(";zzz");
    Sleep(300);
    Check(!FindItem("###commands"), "an unknown command shows no list");
    Tap(VK_ESCAPE);

    Check(!Chat(";help"), ";help is not sent to the server");
    Check(WaitFor([] { return FindItem("###commands"); }, 2000), ";help shows the list after the chat closed");
    Check(Notified("\xD0\x94\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD1\x8B\xD0\xB5 "
                   "\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B (\xD0\xBF\xD1\x80\xD0\xB5\xD1\x84"
                   "\xD0\xB8\xD0\xBA\xD1\x81 ;)"),  // "Доступные команды (префикс ;)"
          ";help also goes to the launcher notifications");
    Tap('T');
    Tap(VK_ESCAPE);
    Check(WaitFor([] { return !FindItem("###commands") && IsClear(Capture(), 400, 300); }, 2000),
          "opening and closing the chat dismisses it");

    // Menu key while the menu cannot open: the player is told why.
    Commands("set Menu.Enabled 0\n");
    const unsigned insertDowns = g_keyDowns[VK_INSERT];
    Tap(VK_INSERT);
    Check(g_keyDowns[VK_INSERT] == insertDowns + 1 &&
              Notified("\xD0\x9C\xD0\xB5\xD0\xBD\xD1\x8E \xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87"
                       "\xD0\xB5\xD0\xBD\xD0\xBE ([Menu] Enabled=0)"),  // "Меню выключено ([Menu] Enabled=0)"
          "INSERT with the menu switched off explains why (and the key goes to the game)");
    Commands("set Menu.Enabled 1\n");
    Check(WaitFor([] { Tap(VK_INSERT); return WaitFor([] { return FindItem("BedrockQoL"); }, 500); }, 5000),
          "the menu opens again after Menu.Enabled 1");
    Tap(VK_ESCAPE);
    WaitFor([] { return IsClear(Capture(), 400, 300); }, 2000);

    Check(!Chat(";status") && Notified("\xD0\x9A\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB8\xD0\xB0\xD1\x82\xD1\x83\xD1\x80"
                                       "\xD0\xB0: \xD1\x85\xD1\x83\xD0\xBA \xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82"
                                       "\xD0\xB0\xD0\xB5\xD1\x82"),  // "Клавиатура: хук работает"
          ";status reports the keyboard hook");

    // RequireHiddenCursor=1 (the default): binds only work in the world, told apart by the cursor.
    Commands("set General.RequireHiddenCursor 1\n");
    const float gamma = Gamma();
    Tap('K');
    Check(WaitFor([&] { return !Near(Gamma(), gamma); }, 2000),
          "the game never hid the cursor yet: the cursor cannot tell, bound K still works");
    g_cursorInWorld = true;  // null cursor, as the UWP game does: flags still say "showing"
    Sleep(100);
    Tap('K');
    Check(WaitFor([&] { return Near(Gamma(), gamma); }, 2000), "null cursor (in the world): K works");
    g_cursorInWorld = false;  // a game screen with an arrow
    Sleep(100);
    Tap('K');
    Sleep(300);
    Check(Near(Gamma(), gamma), "arrow cursor (a game screen is open): K is ignored");
    Check(Notified("K (Fullbright)"), "the player is told once why K did nothing");
    // Some systems show an arrow even in the world; the game then reports only relative movement.
    fake_mouseFeed(nullptr, 0, 0, 400, 300, 5, 0, 0);
    fake_mouseFeed(nullptr, 0, 0, 400, 300, 3, -2, 0);
    Tap('K');
    Check(WaitFor([&] { return !Near(Gamma(), gamma); }, 2000), "mouse captured by the game (relative movement): K works");
    MouseMove(420, 310);  // the pointer moves on a screen again
    Tap('K');
    Sleep(300);
    Check(!Near(Gamma(), gamma), "pointer moving on a screen: K is ignored again");
    fake_mouseFeed(nullptr, 0, 0, 420, 310, 4, 4, 0);
    Tap('K');
    Check(WaitFor([&] { return Near(Gamma(), gamma); }, 2000), "back in the world: K works again");
    Commands("set General.RequireHiddenCursor 0\n");
}

void MenuTests(HMODULE module) {
    const std::wstring status = DataDir() + L"\\control\\status.txt";
    const std::wstring log = DataDir() + L"\\BedrockQoL.log";
    const char* api = g_useD3D12 ? "menu=Direct3D 12" : "menu=Direct3D 11";

    g_findItem = reinterpret_cast<FindItemFn>(reinterpret_cast<void*>(GetProcAddress(module, "BedrockQoL_FindItem")));
    Check(g_findItem != nullptr, "DLL exports BedrockQoL_FindItem (widget lookup for tests)");
    Check(IsHooked(g_presentFn), "IDXGISwapChain::Present of the game's swap chain is hooked");
    Check(WaitFor([&] { return FileContains(status, api); }, 5000),
          g_useD3D12 ? "menu renderer ready (status.txt: menu=Direct3D 12)" : "menu renderer ready (status.txt: menu=Direct3D 11)");
    if (std::getenv("FAKE_GAME_SCREENSHOTS")) Commands("set Menu.Notifications 0\n");  // clean pictures
    Check(IsClear(Capture(), 400, 300), "menu closed: the game's frame is untouched");

    // Opening: the key is swallowed, keys held in the game are released.
    Key('W', true);
    const unsigned insertDowns = g_keyDowns[VK_INSERT];
    Tap(VK_INSERT);
    Check(g_keyDowns[VK_INSERT] == insertDowns, "INSERT opens the menu and does not reach the game");
    Check(WaitFor([] { return FindItem("BedrockQoL"); }, 3000), "menu window is drawn");
    Check(g_keyStates['W'] == 0, "keys held when the menu opens are released in the game");
    Key('W', false);
    Check(WaitFor([] { return !IsClear(Capture(), 400, 300); }, 2000), "menu is visible in the presented frame");
    Screenshot("1-modules");

    // Input belongs to the menu while it is open.
    const unsigned wDowns = g_keyDowns['W'];
    Tap('W');
    Check(g_keyDowns['W'] == wDowns, "keys pressed while the menu is open do not reach the game");
    g_lastMouseButton = -1;
    fake_mouseFeed(nullptr, 4, static_cast<char>(0x88), 400, 300, 0, 0, 0);
    MouseMove(400, 300);
    Check(g_lastMouseButton == -1, "mouse input does not reach the game while the menu is open");

    // Modules page: switches, key binds, settings.
    Check(ClickItem("##toggle.fullbright") && WaitIni("Fullbright", "Enabled", "1"),
          "clicking the Fullbright switch enables it (saved to config.ini)");
    Check(WaitFor([] { return Near(Gamma(), 25.0f); }, 2000), "Fullbright is really on (gamma 25)");
    Check(ClickItem("##toggle.fullbright") && WaitIni("Fullbright", "Enabled", "0"), "clicking it again disables it");
    Check(ClickItem("##bind.Zoom.Key") &&
              WaitFor([] { return FindItem("\xD0\x9D\xD0\xB0\xD0\xB6\xD0\xBC\xD0\xB8\xD1\x82\xD0\xB5 "
                                           "\xD0\xBA\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x88\xD1\x83...##bind.Zoom.Key"); },
                      2000),
          "zoom key button waits for a key");
    Tap('V');
    Check(WaitIni("Zoom", "Key", "V"), "pressing V binds zoom to V (saved to config.ini)");
    Check(WaitFor([] { return FindItem("V##bind.Zoom.Key"); }, 2000), "the button shows the new key");
    Check(ClickItem("##settings.fullbright") && WaitFor([] { return FindItem("##fullbright.gamma"); }, 2000),
          "module settings open inside the card");
    Screenshot("2-module-settings");

    // Other pages.
    Check(ClickItem("##nav.binds") && WaitFor([] { return FindItem("##bind.Menu.Key"); }, 2000),
          "Binds page lists every key, including the menu key");
    Screenshot("3-binds");
    Check(ClickItem("##nav.appearance") && WaitFor([] { return FindItem("##tab.colors"); }, 2000),
          "Appearance page opens");
    Screenshot("4-appearance-colors");
    Check(ClickItem("##preset.6") && WaitIni("Theme", "WindowRounding", "0") && IniGet("Theme", "Accent") == "#5BA02EFF",
          "clicking the Minecraft preset writes its colors and sizes to config.ini [Theme]");
    Screenshot("5-minecraft-preset");
    Check(ClickItem("##tab.sizes") && WaitFor([] { return FindItem("##size.WindowRounding"); }, 2000),
          "Sizes tab lists the style sizes");
    Screenshot("6-sizes");
    Check(ClickItem("##nav.configs") && WaitFor([] { return FindItem("##cfgsave"); }, 2000), "Configs page opens");
    Screenshot("7-configs");

    // Colors from config.ini are what the menu draws.
    float window[4] = {};
    FindItem("BedrockQoL", window);
    const int sideX = static_cast<int>(window[0] + 30);
    const int sideY = static_cast<int>(window[1] + window[3] * 0.8f);
    Commands("set Theme.SidebarBg #FF0000FF\n");
    Check(WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 255, 0, 0); }, 3000),
          "[Theme] SidebarBg=#FF0000FF paints the sidebar red");
    Commands("set Theme.SidebarBg #00000000\nset Theme.WindowBg #00FF00FF\n");
    Check(WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 0, 255, 0); }, 3000),
          "[Theme] WindowBg=#00FF00FF paints the window green");

    // Background picture behind the menu.
    CreateDirectoryW((DataDir() + L"\\images").c_str(), nullptr);
    WriteBmp(DataDir() + L"\\images\\bg.bmp", 0, 0, 255);
    Check(PixelNear(Capture(), 3, 3, 16, 31, 46, 4), "the game is dimmed behind the menu (ScreenDim)");
    Commands("set Menu.DimScreen 0\nset Menu.BackgroundTarget screen\nset Menu.BackgroundOpacity 1\n"
             "set Menu.Background bg.bmp\n");
    Check(WaitFor([] { return PixelNear(Capture(), 3, 3, 0, 0, 255); }, 3000),
          "background picture from BedrockQoL\\images is drawn over the whole screen");
    Commands("set Menu.Background missing.png\n");
    Check(WaitFor([&] { return FileContains(log, "background 'missing.png' not found"); }, 3000) &&
              WaitFor([] { return IsClear(Capture(), 3, 3); }, 3000),
          "a missing picture is reported and not drawn");

    // Fonts: BedrockQoL\fonts, errors fall back to the built-in font.
    CreateDirectoryW((DataDir() + L"\\fonts").c_str(), nullptr);
    Check(CopyFileW((ExeDir() + L"\\test-font.ttf").c_str(), (DataDir() + L"\\fonts\\test.ttf").c_str(), FALSE) != 0,
          "test font copied to BedrockQoL\\fonts");
    Commands("set Menu.Font test.ttf\nset Menu.FontSize 20\n");
    Check(WaitFor([&] { return FileContains(log, "fonts\\test.ttf loaded"); }, 3000), "font from BedrockQoL\\fonts loaded");
    Commands("set Menu.Font nope.ttf\n");
    Check(WaitFor([&] { return FileContains(log, "font 'nope.ttf' not found"); }, 3000) &&
              WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 0, 255, 0); }, 3000),
          "a missing font is reported and the menu keeps working");

    // Appearance is part of every config profile.
    Commands("config save gui1\n");
    Check(WaitFor([] { return IniGetFileW(DataDir() + L"\\configs\\gui1.ini", L"Theme", L"WindowBg") == L"#00FF00FF"; }, 2000),
          "config profile contains the menu theme");
    Commands("set Theme.WindowBg #0000FFFF\n");
    Check(WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 0, 0, 255); }, 3000), "theme changed (blue window)");
    Commands("config load gui1\n");
    Check(WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 0, 255, 0); }, 3000) &&
              IniGet("Theme", "WindowBg") == "#00FF00FF",
          "loading the profile restores its theme");

    // JSON themes.
    Commands("theme save t1\n");
    Check(WaitFor([] { return FileContains(DataDir() + L"\\themes\\t1.json", "\"WindowBg\": \"#00FF00FF\""); }, 2000) &&
              FileContains(DataDir() + L"\\themes\\t1.json", "\"file\": \"nope.ttf\""),
          "theme saved as JSON (themes\\t1.json with colors and font)");
    Commands("theme reset\n");
    Check(WaitFor([] { return IniGet("Theme", "WindowBg").empty() && IniGet("Menu", "Font").empty(); }, 2000),
          "theme reset clears [Theme] and the font");
    Commands("theme load t1\n");
    Check(WaitIni("Theme", "WindowBg", "#00FF00FF") && IniGet("Menu", "Font") == "nope.ttf" &&
              IniGet("Theme", "WindowRounding") == "0",
          "theme loaded back from JSON");

    // The game resizes its buffers (window resized) while the menu is drawn.
    Check(Resize(1024, 640) == S_OK, "ResizeBuffers succeeds while the menu is open (no buffer kept by the menu)");
    Check(WaitFor([&] { return FileContains(status, "1024x640"); }, 3000), "menu follows the new size (1024x640)");
    Check(Resize(kWidth, kHeight) == S_OK, "resizing back succeeds");
    Check(WaitFor([&] { return PixelNear(Capture(), sideX, sideY, 0, 255, 0); }, 3000),
          "menu is drawn again after the resize");

    // Closing.
    const unsigned escDowns = g_keyDowns[VK_ESCAPE];
    Tap(VK_ESCAPE);
    Check(g_keyDowns[VK_ESCAPE] == escDowns, "Esc closes the menu and does not reach the game");
    Check(WaitFor([] { return IsClear(Capture(), 400, 300); }, 2000), "menu closed: nothing drawn over the game");
    Key('W', true);
    Check(g_keyStates['W'] == 1, "keys reach the game again after closing");
    Key('W', false);

    Commands("menu\n");
    Check(WaitFor([] { return !IsClear(Capture(), 400, 300); }, 2000), "the menu command opens the menu");
    Tap(VK_INSERT);
    Check(WaitFor([] { return IsClear(Capture(), 400, 300); }, 2000), "INSERT closes it again");

    CommandListTests();
    Commands("set Menu.Notifications 0\n");
}

// The previous game session died while the menu was starting: the DLL must switch the menu off
// instead of crashing again, tell the player, keep the old log, and come back on request.
void SafeModeTests(const char* dll) {
    const std::wstring log = DataDir() + L"\\BedrockQoL.log";
    const std::wstring guard = DataDir() + L"\\control\\menu_guard.txt";
    WriteFile(guard, "creating the menu renderer");
    HMODULE module = LoadLibraryA(dll);
    Check(module != nullptr, "DLL loads again after a crash during the menu start");
    Check(WaitFor([&] { return FileContains(log, "previous game session ended while creating the menu renderer"); },
                  5000),
          "crash guard found: the menu is switched off");
    Check(WaitIni("Menu", "Enabled", "0"), "[Menu] Enabled=0 saved");
    Check(WaitFor([] { return FileContains(DataDir() + L"\\control\\notifications.txt",
                                           "\xD0\x9C\xD0\xB5\xD0\xBD\xD1\x8E BedrockQoL"); },  // "Меню BedrockQoL"
                  3000),
          "the player is told the menu was switched off");
    Check(FileContains(DataDir() + L"\\BedrockQoL.prev.log", "] Unloaded"),
          "the log of the previous session is kept as BedrockQoL.prev.log");
    WaitFor([&] { return FileContains(log, "] Ready."); }, 5000);

    const unsigned insertDowns = g_keyDowns[VK_INSERT];
    Tap(VK_INSERT);
    Sleep(300);
    Check(g_keyDowns[VK_INSERT] == insertDowns + 1 && IsClear(Capture(), 400, 300),
          "menu switched off: INSERT goes to the game, nothing is drawn");
    Check(Near(Fov(90.0f), 90.0f) && WaitFor([] { Key('V', true); return Near(Fov(90.0f), 22.5f); }, 2000),
          "the other features keep working (zoom on V)");
    Key('V', false);

    if (g_useD3D12) {  // see main(): no DXGI factory while a Direct3D 12 frame is presented under Wine
        g_renderPause = true;
        WaitFor([] { return g_renderPaused.load(); }, 2000);
        Sleep(300);
    }
    Commands("set Menu.Enabled 1\n");
    if (g_useD3D12) {
        WaitFor([&] { return FileContains(log, "Menu: Present hooked") || FileContains(log, "Menu unavailable"); },
                20000);
        g_renderPause = false;
    }
    Check(WaitFor([] { return FileContains(DataDir() + L"\\control\\status.txt", "menu=Direct3D"); }, 5000),
          "Menu.Enabled 1 brings the menu back without restarting the game");
    Tap(VK_INSERT);
    Check(WaitFor([] { return !IsClear(Capture(), 400, 300); }, 3000), "the menu opens again");
    Tap(VK_ESCAPE);

    Commands("unload\n");
    Check(WaitFor([] { return GetModuleHandleA("BedrockQoL.dll") == nullptr; }, 5000), "unloads again");
    Check(GetFileAttributesW(guard.c_str()) == INVALID_FILE_ATTRIBUTES, "no crash guard is left after a clean unload");
}

}  // namespace

int main(int argc, char** argv) {
    g_optionsVtable[12] = nullptr;
    *reinterpret_cast<void**>(g_options) = g_optionsVtable;
    g_options[0x1820] = 1;

    if (argc > 2 && std::strcmp(argv[1], "--host") == 0) return RunHost(std::atoi(argv[2]));

    const char* dll = "BedrockQoL.dll";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--d3d12") == 0) {
            g_useD3D12 = true;
        } else {
            dll = argv[i];
        }
    }
    const ULONGLONG renderStart = GetTickCount64();
    const bool rendering = StartRenderer();
    std::printf("Renderer: %s (%s after %llu ms)\n", g_useD3D12 ? "Direct3D 12" : "Direct3D 11",
                rendering ? "presenting" : g_rendererState == 0 ? "still starting" : "failed",
                GetTickCount64() - renderStart);
    Check(rendering, "fake game renders frames (swap chain created)");
    WriteTestConfig();
    DeleteFileW((DataDir() + L"\\control\\commands.txt").c_str());
    DeleteFileW((DataDir() + L"\\control\\menu_guard.txt").c_str());  // left by a killed earlier run

    Check(Near(Fov(90.0f), 90.0f), "getFov works before injection");
    Check(Near(Gamma(), 1.0f), "getGamma works before injection");

    // Under Wine, creating a DXGI factory (which the DLL does to find IDXGISwapChain::Present) can
    // deadlock inside Mesa's GLX while another thread presents a Vulkan-backed Direct3D 12 swap chain.
    // Windows does not have this problem, and the Direct3D 11 run keeps presenting during injection,
    // so the Direct3D 12 run pauses its render loop until the DLL has set up its hooks.
    DeleteFileW((DataDir() + L"\\BedrockQoL.log").c_str());  // "Ready." below must come from this run
    if (g_useD3D12 && rendering) {
        g_renderPause = true;
        WaitFor([] { return g_renderPaused.load(); }, 2000);
        Sleep(300);  // let the swap chain's own present thread finish the queued frames
    }
    HMODULE module = LoadLibraryA(dll);
    Check(module != nullptr, "DLL loads");
    if (!module) return 1;
    // The menu sets up its Direct3D hooks on the DLL's worker thread shortly after loading; wait for it,
    // so the key tests below do not race with it.
    WaitFor([] { return FileContains(DataDir() + L"\\BedrockQoL.log", "Menu: Present hooked") ||
                        FileContains(DataDir() + L"\\BedrockQoL.log", "Menu unavailable"); },
            20000);
    g_renderPause = false;

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

    // --- In-game menu ---
    if (rendering) MenuTests(module);

    // --- Unload with the menu's button (or a chat command without a renderer) ---
    if (rendering) {
        Tap(VK_INSERT);
        Check(ClickItem("##nav.general") && ClickItem("##general.unload"), "menu button 'Unload' clicked");
    } else {
        Check(!Chat(";unload"), ";unload not sent");
    }
    Check(WaitFor([] { return GetModuleHandleA("BedrockQoL.dll") == nullptr; }, 5000), "unload unloads the DLL");
    Check(!IsHooked((void*)fake_getFov) && !IsHooked((void*)fake_keyboardFeed) && !IsHooked((void*)fake_getGamma),
          "hooks removed after unload");
    Check(Near(Fov(90.0f), 90.0f) && Near(Gamma(), 1.0f), "game functions work after unload");
    Check(Chat(";toggle zoom"), "after unload chat goes to the game again");
    if (rendering) {
        Check(!IsHooked(g_presentFn), "Present unhooked after unload");
        const unsigned frames = g_frames;
        Check(WaitFor([&] { return g_frames > frames + 5; }, 2000) && IsClear(Capture(), 400, 300),
              "the game keeps presenting clean frames after unload");
    }
    if (rendering) SafeModeTests(dll);
    StopRenderer();

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
