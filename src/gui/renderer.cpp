#include "renderer.h"

#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <vector>

#include "../log.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"

// ImGui's DirectX backends compile their two small shaders with D3DCompile at start-up. Linking
// d3dcompiler_47.lib would make the whole DLL fail to load where that DLL is missing, so the call
// is forwarded to whichever d3dcompiler_4x.dll the system has.
extern "C" HRESULT WINAPI D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO* defines,
                                     ID3DInclude* include, LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2,
                                     ID3DBlob** code, ID3DBlob** errors) {
    using Fn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                                UINT, ID3DBlob**, ID3DBlob**);
    static Fn fn = [] {
        for (const wchar_t* dll : {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"}) {
            if (HMODULE module = LoadLibraryW(dll)) {
                if (auto p = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, "D3DCompile")))) {
                    return p;
                }
            }
        }
        logx::Error("Menu: no d3dcompiler_4x.dll found, cannot compile the menu shaders");
        return static_cast<Fn>(nullptr);
    }();
    return fn ? fn(data, size, name, defines, include, entry, target, flags1, flags2, code, errors) : E_NOTIMPL;
}

namespace gui {
namespace {

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Render target views need a typed format.
DXGI_FORMAT TypedFormat(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return format;
    }
}

// The game's window. Swap chains made with CreateSwapChainForCoreWindow (UWP) have no OutputWindow;
// the CoreWindow object is not called from the render thread (it belongs to the UI thread), the
// window is looked up instead: a visible window of this process, or its UWP CoreWindow.
BOOL CALLBACK FindProcessWindow(HWND hwnd, LPARAM param) {
    auto* found = reinterpret_cast<HWND*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
        *found = hwnd;
        return FALSE;
    }
    if (HWND core = FindWindowExW(hwnd, nullptr, L"Windows.UI.Core.CoreWindow", nullptr)) {
        GetWindowThreadProcessId(core, &pid);
        if (pid == GetCurrentProcessId()) {
            *found = core;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindSwapChainWindow(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) return desc.OutputWindow;
    HWND found = nullptr;
    EnumWindows(&FindProcessWindow, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool Readable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    return (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// DXGI keeps a pointer to the command queue a Direct3D 12 swap chain was created with inside the
// swap chain object. Drawing on that queue (not just any direct queue of the game) keeps the menu's
// commands ordered with the game's frame. Returns null if none of `queues` is found there.
ID3D12CommandQueue* FindSwapChainQueue(IDXGISwapChain* swapChain, const std::vector<ID3D12CommandQueue*>& queues,
                                       ptrdiff_t& offset) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (queues.empty() || !VirtualQuery(swapChain, &mbi, sizeof(mbi)) || !Readable(mbi)) return nullptr;
    const uintptr_t object = reinterpret_cast<uintptr_t>(swapChain);
    const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd = regionBegin + mbi.RegionSize;
    const uintptr_t begin = object - regionBegin > 0x400 ? object - 0x400 : regionBegin;
    const uintptr_t end = regionEnd - object > 0x1000 ? object + 0x1000 : regionEnd;
    for (uintptr_t p = begin & ~static_cast<uintptr_t>(7); p + sizeof(void*) <= end; p += sizeof(void*)) {
        const void* value = *reinterpret_cast<void* const*>(p);
        for (ID3D12CommandQueue* queue : queues) {
            if (value == queue) {
                offset = static_cast<ptrdiff_t>(p - object);
                return queue;
            }
        }
    }
    return nullptr;
}

std::string FormatName(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
        default: return "format " + std::to_string(static_cast<int>(format));
    }
}

// --- Direct3D 11 ---------------------------------------------------------------------------

class D3D11Renderer final : public Renderer {
public:
    D3D11Renderer(IDXGISwapChain* swapChain, ID3D11Device* device) : Renderer(swapChain), device_(device) {
        device_->GetImmediateContext(&context_);
    }
    ~D3D11Renderer() override { Shutdown(); }

    const char* Name() const override { return "Direct3D 11"; }

    bool Init(std::string& error) override {
        backend_ = ImGui_ImplDX11_Init(device_, context_);
        if (!backend_) error = "ImGui_ImplDX11_Init failed";
        return backend_;
    }

    void NewFrame() override { ImGui_ImplDX11_NewFrame(); }

    void Render(ImDrawData* data) override {
        // The view is created per frame and released right away, so the game can resize the
        // buffers at any time.
        ID3D11Texture2D* buffer = nullptr;
        if (FAILED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return;
        D3D11_TEXTURE2D_DESC td{};
        buffer->GetDesc(&td);
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = TypedFormat(td.Format);
        rd.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        ID3D11RenderTargetView* view = nullptr;
        const HRESULT hr = device_->CreateRenderTargetView(buffer, &rd, &view);
        buffer->Release();
        if (FAILED(hr)) return;

        ID3D11RenderTargetView* oldViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* oldDepth = nullptr;
        context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldViews, &oldDepth);

        context_->OMSetRenderTargets(1, &view, nullptr);
        ImGui_ImplDX11_RenderDrawData(data);  // saves and restores the rest of the pipeline state

        context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldViews, oldDepth);
        for (ID3D11RenderTargetView*& v : oldViews) SafeRelease(v);
        SafeRelease(oldDepth);
        view->Release();
    }

    void BeforeResize() override {}

    void Shutdown() override {
        if (backend_) ImGui_ImplDX11_Shutdown();
        backend_ = false;
        SafeRelease(context_);
        SafeRelease(device_);
    }

    bool SameDevice(IDXGISwapChain* swapChain) const override {
        ID3D11Device* device = nullptr;
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
        device->Release();
        return device == device_;
    }

    bool DeviceLost(HRESULT& reason) const override {
        reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
        return FAILED(reason);
    }

    std::string Details() const override {
        DXGI_SWAP_CHAIN_DESC desc{};
        swapChain_->GetDesc(&desc);
        return std::string(Name()) + ", " + std::to_string(desc.BufferDesc.Width) + "x" +
               std::to_string(desc.BufferDesc.Height) + ", " + FormatName(desc.BufferDesc.Format) + ", " +
               std::to_string(desc.BufferCount) + " buffer(s)";
    }

private:
    ID3D11Device* device_;  // owned reference
    ID3D11DeviceContext* context_ = nullptr;
    bool backend_ = false;
};

// --- Direct3D 12 ---------------------------------------------------------------------------

class D3D12Renderer final : public Renderer {
public:
    // No reference to the swap chain or its buffers is kept between frames: the game must stay free
    // to resize or recreate them (DXGI refuses a new swap chain for a window whose old one is alive).
    D3D12Renderer(IDXGISwapChain3* swapChain, ID3D12Device* device, ID3D12CommandQueue* queue, std::string queueSource)
        : Renderer(swapChain), chain3_(swapChain), device_(device), queue_(queue), queueSource_(std::move(queueSource)) {
        queue_->AddRef();
    }
    ~D3D12Renderer() override { Shutdown(); }

    const char* Name() const override { return "Direct3D 12"; }

    bool Init(std::string& error) override {
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                        reinterpret_cast<void**>(&fence_)))) {
            error = "CreateFence failed";
            return false;
        }
        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        D3D12_DESCRIPTOR_HEAP_DESC srv{};
        srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv.NumDescriptors = kSrvCount;
        srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device_->CreateDescriptorHeap(&srv, __uuidof(ID3D12DescriptorHeap),
                                                 reinterpret_cast<void**>(&srvHeap_)))) {
            error = "CreateDescriptorHeap (SRV) failed";
            return false;
        }
        srvSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (int i = kSrvCount - 1; i >= 0; --i) freeSrv_.push_back(i);

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain_->GetDesc(&desc)) || !CreateFrames(desc, error)) {
            if (error.empty()) error = "IDXGISwapChain::GetDesc failed";
            return false;
        }
        return InitBackend(error);
    }

    void NewFrame() override { ImGui_ImplDX12_NewFrame(); }

    void Render(ImDrawData* data) override {
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapChain_->GetDesc(&desc))) return;
        if (desc.BufferCount != frames_.size() || TypedFormat(desc.BufferDesc.Format) != format_) {
            // Buffers were resized to another count or format.
            WaitIdle();
            DestroyFrames();
            std::string error;
            if (!CreateFrames(desc, error)) {
                logx::Error("Menu (Direct3D 12): %s", error.c_str());
                return;
            }
        }
        const UINT index = chain3_->GetCurrentBackBufferIndex();
        if (index >= frames_.size()) return;
        Frame& frame = frames_[index];
        Wait(frame.fenceValue);

        ID3D12Resource* buffer = nullptr;
        if (FAILED(chain3_->GetBuffer(index, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&buffer)))) return;
        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = format_;
        rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(buffer, &rd, frame.rtv);

        frame.allocator->Reset();
        list_->Reset(frame.allocator, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list_->ResourceBarrier(1, &barrier);

        list_->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
        list_->SetDescriptorHeaps(1, &srvHeap_);
        ImGui_ImplDX12_RenderDrawData(data, list_);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list_->ResourceBarrier(1, &barrier);
        list_->Close();

        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        queue_->Signal(fence_, ++fenceValue_);
        frame.fenceValue = fenceValue_;
        buffer->Release();  // the swap chain owns it; the GPU work above is tracked by the fence
    }

    void BeforeResize() override { WaitIdle(); }

    bool SameDevice(IDXGISwapChain* swapChain) const override {
        ID3D12Device* device = nullptr;
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device)))) return false;
        device->Release();
        return device == device_;
    }

    bool DeviceLost(HRESULT& reason) const override {
        reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
        return FAILED(reason);
    }

    std::string Details() const override {
        DXGI_SWAP_CHAIN_DESC desc{};
        swapChain_->GetDesc(&desc);
        return std::string(Name()) + ", " + std::to_string(desc.BufferDesc.Width) + "x" +
               std::to_string(desc.BufferDesc.Height) + ", " + FormatName(desc.BufferDesc.Format) + ", " +
               std::to_string(desc.BufferCount) + " buffers, " + queueSource_;
    }

    void Shutdown() override {
        WaitIdle();
        if (backend_) ImGui_ImplDX12_Shutdown();
        backend_ = false;
        DestroyFrames();
        SafeRelease(srvHeap_);
        SafeRelease(fence_);
        if (fenceEvent_) {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        SafeRelease(queue_);
        SafeRelease(device_);
    }

private:
    static constexpr int kSrvCount = 64;

    struct Frame {
        ID3D12CommandAllocator* allocator = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        UINT64 fenceValue = 0;
    };

    bool InitBackend(std::string& error) {
        ImGui_ImplDX12_InitInfo info;
        info.Device = device_;
        info.CommandQueue = queue_;
        info.NumFramesInFlight = static_cast<int>(frames_.size());
        info.RTVFormat = format_;
        info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        info.UserData = this;
        info.SrvDescriptorHeap = srvHeap_;
        info.SrvDescriptorAllocFn = &AllocSrv;
        info.SrvDescriptorFreeFn = &FreeSrv;
        backend_ = ImGui_ImplDX12_Init(&info);
        backendFormat_ = format_;
        if (!backend_) error = "ImGui_ImplDX12_Init failed";
        return backend_;
    }

    bool CreateFrames(const DXGI_SWAP_CHAIN_DESC& desc, std::string& error) {
        if (desc.BufferCount == 0) {
            error = "the swap chain has no buffers";
            return false;
        }
        format_ = TypedFormat(desc.BufferDesc.Format);

        D3D12_DESCRIPTOR_HEAP_DESC rtv{};
        rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv.NumDescriptors = desc.BufferCount;
        if (FAILED(device_->CreateDescriptorHeap(&rtv, __uuidof(ID3D12DescriptorHeap),
                                                 reinterpret_cast<void**>(&rtvHeap_)))) {
            error = "CreateDescriptorHeap (RTV) failed";
            return false;
        }
        const UINT rtvSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

        frames_.resize(desc.BufferCount);
        for (Frame& f : frames_) {
            f.rtv = handle;
            handle.ptr += rtvSize;
            if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                       reinterpret_cast<void**>(&f.allocator)))) {
                error = "CreateCommandAllocator failed";
                DestroyFrames();
                return false;
            }
        }
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator, nullptr,
                                              __uuidof(ID3D12GraphicsCommandList),
                                              reinterpret_cast<void**>(&list_)))) {
            error = "CreateCommandList failed";
            DestroyFrames();
            return false;
        }
        list_->Close();

        // The pipeline is built for one render target format: rebuild the backend if it changed.
        if (backend_ && backendFormat_ != format_) {
            ImGui_ImplDX12_Shutdown();
            backend_ = false;
            return InitBackend(error);
        }
        return true;
    }

    void DestroyFrames() {
        for (Frame& f : frames_) SafeRelease(f.allocator);
        frames_.clear();
        SafeRelease(list_);
        SafeRelease(rtvHeap_);
    }

    void Wait(UINT64 value) {
        if (!fence_ || value == 0 || fence_->GetCompletedValue() >= value) return;
        fence_->SetEventOnCompletion(value, fenceEvent_);
        WaitForSingleObject(fenceEvent_, 2000);
    }

    void WaitIdle() {
        if (!fence_ || !queue_) return;
        queue_->Signal(fence_, ++fenceValue_);
        Wait(fenceValue_);
    }

    static void AllocSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                         D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* self = static_cast<D3D12Renderer*>(info->UserData);
        cpu->ptr = 0;
        gpu->ptr = 0;
        if (self->freeSrv_.empty()) {
            logx::Error("Menu (Direct3D 12): out of texture descriptors");
            return;
        }
        const int index = self->freeSrv_.back();
        self->freeSrv_.pop_back();
        cpu->ptr = self->srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(index) * self->srvSize_;
        gpu->ptr = self->srvHeap_->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(index) * self->srvSize_;
    }

    static void FreeSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto* self = static_cast<D3D12Renderer*>(info->UserData);
        if (cpu.ptr == 0 || !self->srvHeap_) return;
        const SIZE_T start = self->srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
        self->freeSrv_.push_back(static_cast<int>((cpu.ptr - start) / self->srvSize_));
    }

    IDXGISwapChain3* chain3_;  // not owned, like swapChain_
    ID3D12Device* device_;     // owned reference
    ID3D12CommandQueue* queue_;
    std::string queueSource_;
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    ID3D12DescriptorHeap* rtvHeap_ = nullptr;
    UINT srvSize_ = 0;
    std::vector<int> freeSrv_;
    std::vector<Frame> frames_;
    ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValue_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT backendFormat_ = DXGI_FORMAT_UNKNOWN;
    bool backend_ = false;
};

}  // namespace

Renderer::Renderer(IDXGISwapChain* swapChain) : swapChain_(swapChain) {
    window_ = FindSwapChainWindow(swapChain);
    UpdateSize();
}

void Renderer::UpdateSize() {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swapChain_->GetDesc(&desc))) {
        width_ = desc.BufferDesc.Width;
        height_ = desc.BufferDesc.Height;
    }
}

std::unique_ptr<Renderer> CreateRenderer(IDXGISwapChain* swapChain, const std::vector<ID3D12CommandQueue*>& queues,
                                         ID3D12CommandQueue* lastQueue, bool& retry, std::string& error) {
    retry = false;
    ID3D11Device* device11 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device11)))) {
        return std::make_unique<D3D11Renderer>(swapChain, device11);
    }

    ID3D12Device* device12 = nullptr;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)))) {
        error = "the swap chain belongs to neither a Direct3D 11 nor a Direct3D 12 device";
        return nullptr;
    }
    IDXGISwapChain3* chain3 = nullptr;
    if (FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain3)))) {
        device12->Release();
        error = "the Direct3D 12 swap chain has no IDXGISwapChain3";
        return nullptr;
    }
    chain3->Release();  // same object as `swapChain`, which the game keeps alive while presenting it

    // Only queues of the swap chain's own device can draw into its buffers.
    auto sameDevice = [device12](ID3D12CommandQueue* queue) {
        ID3D12Device* device = nullptr;
        if (!queue || FAILED(queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device)))) return false;
        device->Release();
        return device == device12;
    };
    std::vector<ID3D12CommandQueue*> candidates;
    for (ID3D12CommandQueue* queue : queues) {
        if (sameDevice(queue)) candidates.push_back(queue);
    }
    ptrdiff_t offset = 0;
    ID3D12CommandQueue* queue = FindSwapChainQueue(swapChain, candidates, offset);
    std::string source;
    if (queue) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "swap chain queue (at %+lld)", static_cast<long long>(offset));
        source = buffer;
    } else if (sameDevice(lastQueue)) {
        queue = lastQueue;
        source = "last direct queue of the game (" + std::to_string(candidates.size()) + " seen)";
    }
    if (!queue) {
        device12->Release();
        retry = true;  // the game's command queue has not been seen in ExecuteCommandLists yet
        return nullptr;
    }
    return std::make_unique<D3D12Renderer>(chain3, device12, queue, source);
}

}  // namespace gui
