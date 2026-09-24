#include "renderer.h"

#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

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

// Windows.UI.Core.ICoreWindowInterop: the HWND behind a UWP CoreWindow (swap chains created with
// CreateSwapChainForCoreWindow have no OutputWindow).
struct ICoreWindowInteropBqol : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(unsigned char value) = 0;
};
const GUID kIID_ICoreWindowInterop = {0x45D64A29, 0xA63E, 0x4CB6, {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};

HWND FindSwapChainWindow(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.OutputWindow) return desc.OutputWindow;

    HWND hwnd = nullptr;
    IDXGISwapChain1* chain1 = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1)))) {
        IUnknown* core = nullptr;
        if (SUCCEEDED(chain1->GetCoreWindow(__uuidof(IUnknown), reinterpret_cast<void**>(&core))) && core) {
            ICoreWindowInteropBqol* interop = nullptr;
            if (SUCCEEDED(core->QueryInterface(kIID_ICoreWindowInterop, reinterpret_cast<void**>(&interop)))) {
                interop->get_WindowHandle(&hwnd);
                interop->Release();
            }
            core->Release();
        }
        if (!hwnd) chain1->GetHwnd(&hwnd);
        chain1->Release();
    }
    return hwnd;
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
    D3D12Renderer(IDXGISwapChain3* swapChain, ID3D12Device* device, ID3D12CommandQueue* queue)
        : Renderer(swapChain), chain3_(swapChain), device_(device), queue_(queue) {
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

std::unique_ptr<Renderer> CreateRenderer(IDXGISwapChain* swapChain, ID3D12CommandQueue* queue, bool& retry,
                                         std::string& error) {
    retry = false;
    ID3D11Device* device11 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device11)))) {
        return std::make_unique<D3D11Renderer>(swapChain, device11);
    }

    ID3D12Device* device12 = nullptr;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device12)))) {
        IDXGISwapChain3* chain3 = nullptr;
        if (FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&chain3)))) {
            device12->Release();
            error = "the Direct3D 12 swap chain has no IDXGISwapChain3";
            return nullptr;
        }
        ID3D12Device* queueDevice = nullptr;
        if (queue) queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&queueDevice));
        const bool sameDevice = queueDevice == device12;
        SafeRelease(queueDevice);
        if (!sameDevice) {
            chain3->Release();
            device12->Release();
            retry = true;  // the game's command queue has not been seen in ExecuteCommandLists yet
            return nullptr;
        }
        chain3->Release();  // same object as `swapChain`, which the game keeps alive while presenting it
        return std::make_unique<D3D12Renderer>(chain3, device12, queue);
    }

    error = "the swap chain belongs to neither a Direct3D 11 nor a Direct3D 12 device";
    return nullptr;
}

}  // namespace gui
