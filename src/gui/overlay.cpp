#include "overlay.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../log.h"
#include "MinHook.h"
#include "automation.h"
#include "imgui.h"
#include "menu.h"
#include "renderer.h"
#include "state.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace gui::overlay {
namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
                                                     const UINT*, IUnknown* const*);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

// vtable slots (IDXGISwapChain3 / ID3D12CommandQueue ABI).
constexpr int kPresent = 8;
constexpr int kResizeBuffers = 13;
constexpr int kPresent1 = 22;
constexpr int kResizeBuffers1 = 39;
constexpr int kExecuteCommandLists = 10;

// Direct3D 11 and Direct3D 12 swap chains may be implemented by different functions (they are
// under Wine), so every entry point can exist twice.
PresentFn g_present[2] = {};
Present1Fn g_present1[2] = {};
ResizeBuffersFn g_resizeBuffers[2] = {};
ResizeBuffers1Fn g_resizeBuffers1 = nullptr;
ExecuteCommandListsFn g_executeCommandLists = nullptr;
std::vector<void*> g_targets;

std::atomic<ID3D12CommandQueue*> g_queue{nullptr};  // last direct queue the game submitted work to
std::atomic<ID3D12CommandQueue*> g_ownQueue{nullptr};  // the throw-away queue of HookD3D12 (not the game's)

enum class Phase { Running, ShutdownRequested, Stopped };
std::atomic<Phase> g_phase{Phase::Running};
HANDLE g_stoppedEvent = nullptr;
SRWLOCK g_frameLock = SRWLOCK_INIT;  // one frame / resize / shutdown at a time

// Render thread state (under g_frameLock).
std::unique_ptr<Renderer> g_renderer;
IDXGISwapChain* g_failedSwapChain = nullptr;  // do not retry a swap chain that cannot be used
bool g_imgui = false;
LARGE_INTEGER g_lastFrameTime{};
LARGE_INTEGER g_frequency{};

std::atomic<ULONGLONG> g_lastPresent{0};
std::atomic<bool> g_rendererOk{false};
std::atomic<HWND> g_window{nullptr};
std::atomic<unsigned> g_width{0};
std::atomic<unsigned> g_height{0};
SRWLOCK g_statusLock = SRWLOCK_INIT;
std::string g_status = "меню ещё не нарисовано: игра не показала ни одного кадра";

thread_local int t_presentDepth = 0;

void SetStatus(const std::string& status) {
    AcquireSRWLockExclusive(&g_statusLock);
    g_status = status;
    ReleaseSRWLockExclusive(&g_statusLock);
}

void DestroyAll() {
    g_rendererOk = false;
    if (g_renderer) g_renderer->Shutdown();
    if (g_imgui) {
        menu::Shutdown();  // user textures, after the backend released their GPU copies
        ImGui::DestroyContext();
        g_imgui = false;
    }
    g_renderer.reset();
}

bool CreateAll(IDXGISwapChain* swapChain) {
    bool retry = false;
    std::string error;
    std::unique_ptr<Renderer> renderer = CreateRenderer(swapChain, g_queue.load(), retry, error);
    if (!renderer) {
        if (!retry) {
            g_failedSwapChain = swapChain;
            logx::Error("Menu unavailable: %s", error.c_str());
            SetStatus("меню недоступно: " + error);
        }
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_imgui = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // window positions are not saved; sizes live in config.ini
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    menu::Init();

    if (!renderer->Init(error)) {
        logx::Error("Menu unavailable (%s): %s", renderer->Name(), error.c_str());
        SetStatus(std::string("меню недоступно: ") + error);
        g_renderer = std::move(renderer);
        DestroyAll();
        g_failedSwapChain = swapChain;
        return false;
    }
    g_renderer = std::move(renderer);
    g_window = g_renderer->Window();
    logx::Info("Menu ready: %s, %ux%u", g_renderer->Name(), g_renderer->Width(), g_renderer->Height());
    SetStatus(std::string(g_renderer->Name()));
    g_rendererOk = true;
    return true;
}

void Frame(IDXGISwapChain* swapChain) {
    const Phase phase = g_phase.load();
    if (phase == Phase::Stopped) return;

    AcquireSRWLockExclusive(&g_frameLock);
    if (phase == Phase::ShutdownRequested) {
        DestroyAll();
        g_phase = Phase::Stopped;
        SetEvent(g_stoppedEvent);
        ReleaseSRWLockExclusive(&g_frameLock);
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (!g_renderer || g_renderer->SwapChain() != swapChain) {
        // Another swap chain while ours is still presenting (e.g. a second window): leave it alone.
        if (g_renderer && now - g_lastPresent.load() < 2000) {
            ReleaseSRWLockExclusive(&g_frameLock);
            return;
        }
        if (swapChain == g_failedSwapChain) {
            ReleaseSRWLockExclusive(&g_frameLock);
            return;
        }
        if (g_renderer) {
            logx::Info("Menu: the game switched to a new swap chain, recreating the menu");
            DestroyAll();
        }
        if (!CreateAll(swapChain)) {
            ReleaseSRWLockExclusive(&g_frameLock);
            return;
        }
    }
    g_lastPresent = now;

    Renderer& renderer = *g_renderer;
    renderer.UpdateSize();
    g_width = renderer.Width();
    g_height = renderer.Height();

    if (menu::WantsFrame() && renderer.Width() > 0 && renderer.Height() > 0) {
        ImGuiIO& io = ImGui::GetIO();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
        float dt = g_lastFrameTime.QuadPart
                       ? static_cast<float>(t.QuadPart - g_lastFrameTime.QuadPart) / static_cast<float>(g_frequency.QuadPart)
                       : 1.0f / 60.0f;
        g_lastFrameTime = t;
        io.DeltaTime = dt > 0.0f ? (dt < 0.25f ? dt : 0.25f) : 1.0f / 60.0f;
        io.DisplaySize = ImVec2(static_cast<float>(renderer.Width()), static_cast<float>(renderer.Height()));

        menu::BeginFrame(io);
        automation::BeginFrame();
        renderer.NewFrame();
        ImGui::NewFrame();
        menu::Draw();
        ImGui::Render();
        renderer.Render(ImGui::GetDrawData());
        automation::EndFrame();
        FlushWrites();
    } else {
        g_lastFrameTime.QuadPart = 0;
    }
    ReleaseSRWLockExclusive(&g_frameLock);
}

void OnPresent(IDXGISwapChain* swapChain, UINT flags) {
    // Present1 may be implemented on top of Present (or the other way round): draw once.
    if (t_presentDepth > 0 || (flags & DXGI_PRESENT_TEST)) return;
    ++t_presentDepth;
    Frame(swapChain);
    --t_presentDepth;
}

void OnResize(IDXGISwapChain* swapChain) {
    if (g_phase.load() != Phase::Running) return;
    AcquireSRWLockExclusive(&g_frameLock);
    if (g_renderer && g_renderer->SwapChain() == swapChain) g_renderer->BeforeResize();
    ReleaseSRWLockExclusive(&g_frameLock);
}

template <int N>
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swapChain, UINT sync, UINT flags) {
    OnPresent(swapChain, flags);
    ++t_presentDepth;
    const HRESULT hr = g_present[N](swapChain, sync, flags);
    --t_presentDepth;
    return hr;
}

template <int N>
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* swapChain, UINT sync, UINT flags,
                                       const DXGI_PRESENT_PARAMETERS* params) {
    OnPresent(swapChain, flags);
    ++t_presentDepth;
    const HRESULT hr = g_present1[N](swapChain, sync, flags, params);
    --t_presentDepth;
    return hr;
}

template <int N>
HRESULT STDMETHODCALLTYPE HookResizeBuffers(IDXGISwapChain* swapChain, UINT count, UINT width, UINT height,
                                            DXGI_FORMAT format, UINT flags) {
    OnResize(swapChain);
    return g_resizeBuffers[N](swapChain, count, width, height, format, flags);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers1(IDXGISwapChain3* swapChain, UINT count, UINT width, UINT height,
                                             DXGI_FORMAT format, UINT flags, const UINT* nodeMasks,
                                             IUnknown* const* queues) {
    OnResize(swapChain);
    return g_resizeBuffers1(swapChain, count, width, height, format, flags, nodeMasks, queues);
}

void STDMETHODCALLTYPE HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count,
                                               ID3D12CommandList* const* lists) {
    if (g_queue.load(std::memory_order_relaxed) != queue && queue != g_ownQueue.load(std::memory_order_relaxed) &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        g_queue.store(queue);
    }
    g_executeCommandLists(queue, count, lists);
}

void** VTable(void* object) { return *static_cast<void***>(object); }

bool Hook(void* target, void* detour, void** original, const char* name) {
    for (void* t : g_targets) {
        if (t == target) return false;  // same function as an entry point hooked already
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK) status = MH_EnableHook(target);
    if (status != MH_OK) {
        logx::Error("Menu: hooking %s failed (%s)", name, MH_StatusToString(status));
        MH_RemoveHook(target);
        *original = nullptr;
        return false;
    }
    g_targets.push_back(target);
    return true;
}

struct DummyWindow {
    HWND hwnd = nullptr;
    const wchar_t* className = L"BedrockQoLDummySwapChain";
    DummyWindow() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(0, className, L"BedrockQoL", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                               wc.hInstance, nullptr);
    }
    ~DummyWindow() {
        if (hwnd) DestroyWindow(hwnd);
        UnregisterClassW(className, reinterpret_cast<HINSTANCE>(&__ImageBase));
    }
};

// Direct3D 11: a throw-away device + swap chain gives the addresses of DXGI's swap chain methods.
bool HookD3D11(HWND hwnd, int slot) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = E_FAIL;
    for (D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain,
                                           &device, nullptr, &context);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) {
        logx::Warn("Menu: Direct3D 11 test device failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }

    void** vt = VTable(chain);
    bool ok = false;
    ok |= Hook(vt[kPresent], slot == 0 ? reinterpret_cast<void*>(&HookPresent<0>) : reinterpret_cast<void*>(&HookPresent<1>),
               reinterpret_cast<void**>(&g_present[slot]), "IDXGISwapChain::Present");
    Hook(vt[kResizeBuffers],
         slot == 0 ? reinterpret_cast<void*>(&HookResizeBuffers<0>) : reinterpret_cast<void*>(&HookResizeBuffers<1>),
         reinterpret_cast<void**>(&g_resizeBuffers[slot]), "IDXGISwapChain::ResizeBuffers");
    IDXGISwapChain1* chain1 = nullptr;
    if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1)))) {
        ok |= Hook(VTable(chain1)[kPresent1],
                   slot == 0 ? reinterpret_cast<void*>(&HookPresent1<0>) : reinterpret_cast<void*>(&HookPresent1<1>),
                   reinterpret_cast<void**>(&g_present1[slot]), "IDXGISwapChain1::Present1");
        chain1->Release();
    }
    chain->Release();
    context->Release();
    device->Release();
    return ok;
}

// Direct3D 12: the game's command queue is needed to draw, so ExecuteCommandLists is hooked too.
bool HookD3D12(HWND hwnd, int slot) {
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12) return false;  // the game does not use Direct3D 12
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto createDevice =
        reinterpret_cast<CreateDeviceFn>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice")));
    if (!createDevice) return false;

    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain1* chain = nullptr;
    bool ok = false;

    HRESULT hr = createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (SUCCEEDED(hr)) {
        hr = device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue));
    }
    if (SUCCEEDED(hr)) hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 64;
        desc.Height = 64;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &chain);
    }
    if (SUCCEEDED(hr)) {
        void** vt = VTable(chain);
        ok |= Hook(vt[kPresent],
                   slot == 0 ? reinterpret_cast<void*>(&HookPresent<0>) : reinterpret_cast<void*>(&HookPresent<1>),
                   reinterpret_cast<void**>(&g_present[slot]), "IDXGISwapChain::Present (D3D12)");
        ok |= Hook(vt[kPresent1],
                   slot == 0 ? reinterpret_cast<void*>(&HookPresent1<0>) : reinterpret_cast<void*>(&HookPresent1<1>),
                   reinterpret_cast<void**>(&g_present1[slot]), "IDXGISwapChain1::Present1 (D3D12)");
        Hook(vt[kResizeBuffers],
             slot == 0 ? reinterpret_cast<void*>(&HookResizeBuffers<0>) : reinterpret_cast<void*>(&HookResizeBuffers<1>),
             reinterpret_cast<void**>(&g_resizeBuffers[slot]), "IDXGISwapChain::ResizeBuffers (D3D12)");
        IDXGISwapChain3* chain3 = nullptr;
        if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain3)))) {
            Hook(VTable(chain3)[kResizeBuffers1], reinterpret_cast<void*>(&HookResizeBuffers1),
                 reinterpret_cast<void**>(&g_resizeBuffers1), "IDXGISwapChain3::ResizeBuffers1");
            chain3->Release();
        }
        if (!Hook(VTable(queue)[kExecuteCommandLists], reinterpret_cast<void*>(&HookExecuteCommandLists),
                  reinterpret_cast<void**>(&g_executeCommandLists), "ID3D12CommandQueue::ExecuteCommandLists")) {
            ok = false;  // without the game's queue the menu cannot be drawn with Direct3D 12
        }
    } else {
        logx::Warn("Menu: Direct3D 12 test swap chain failed (0x%08lX)", static_cast<unsigned long>(hr));
    }

    g_ownQueue = queue;
    if (chain) chain->Release();
    if (factory) factory->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    g_ownQueue = nullptr;
    return ok;
}

}  // namespace

bool Install() {
    g_stoppedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DummyWindow window;
    if (!window.hwnd) {
        logx::Error("Menu unavailable: could not create a window for the Direct3D test device");
        SetStatus("меню недоступно: не удалось создать тестовое окно Direct3D");
        return false;
    }
    // A Direct3D 12 game is recognised by its command queue submitting work; a throw-away Direct3D 11
    // device is only created when the game does not (d3d12.dll may be loaded just to probe support).
    const bool d3d12 = HookD3D12(window.hwnd, 1);
    bool d3d11 = false;
    const ULONGLONG deadline = GetTickCount64() + 1500;
    while (d3d12 && !g_queue.load() && GetTickCount64() < deadline) Sleep(20);
    if (!g_queue.load()) d3d11 = HookD3D11(window.hwnd, 0);
    if (!d3d11 && !d3d12) {
        logx::Error("Menu unavailable: IDXGISwapChain::Present could not be hooked");
        SetStatus("меню недоступно: не удалось перехватить IDXGISwapChain::Present");
        return false;
    }
    logx::Info("Menu: Present hooked (%s%s%s), waiting for the first frame", d3d11 ? "Direct3D 11" : "",
               d3d11 && d3d12 ? " + " : "", d3d12 ? "Direct3D 12" : "");
    return true;
}

void Shutdown() {
    SetMenuOpen(false);
    if (g_phase.load() == Phase::Stopped || !g_stoppedEvent) return;
    g_phase = Phase::ShutdownRequested;

    // Let the render thread tear everything down on its next frame...
    if (WaitForSingleObject(g_stoppedEvent, 1500) != WAIT_OBJECT_0) {
        // ...or, if the game stopped presenting (minimised, loading), do it here with the hooks off.
        for (void* target : g_targets) MH_DisableHook(target);
        AcquireSRWLockExclusive(&g_frameLock);
        if (g_phase.load() != Phase::Stopped) {
            DestroyAll();
            g_phase = Phase::Stopped;
        }
        ReleaseSRWLockExclusive(&g_frameLock);
    }
    CloseHandle(g_stoppedEvent);
    g_stoppedEvent = nullptr;
    logx::Info("Menu shut down");
}

bool Ready() { return g_rendererOk.load() && GetTickCount64() - g_lastPresent.load() < 1000; }

std::string Status() {
    AcquireSRWLockShared(&g_statusLock);
    std::string status = g_status;
    ReleaseSRWLockShared(&g_statusLock);
    if (g_rendererOk.load()) status += ", " + std::to_string(g_width.load()) + "x" + std::to_string(g_height.load());
    return status;
}

void DisplaySize(float& width, float& height) {
    width = static_cast<float>(g_width.load());
    height = static_cast<float>(g_height.load());
}

HWND GameWindow() { return g_window.load(); }

}  // namespace gui::overlay
