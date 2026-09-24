#pragma once

#include <windows.h>
#include <dxgi.h>

#include <memory>
#include <string>
#include <vector>

struct ImDrawData;
struct ID3D12CommandQueue;

namespace gui {

// Draws ImGui into the game's swap chain with the game's own device. Render thread only.
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual const char* Name() const = 0;

    // Creates the ImGui backend (an ImGui context must be current).
    virtual bool Init(std::string& error) = 0;
    virtual void NewFrame() = 0;

    // Draws into the swap chain's current back buffer, right before the game presents it.
    virtual void Render(ImDrawData* data) = 0;

    // The game is about to resize or recreate the swap chain buffers: drop every reference to them.
    virtual void BeforeResize() = 0;

    // Destroys the ImGui backend and every GPU object (waits for the GPU).
    virtual void Shutdown() = 0;

    // The swap chain still belongs to the device this renderer was made for (the game may recreate
    // its device and get a new swap chain at the same address).
    virtual bool SameDevice(IDXGISwapChain* swapChain) const = 0;

    // The device was removed (driver reset, or a bad command): nothing can be drawn with it any more.
    virtual bool DeviceLost(HRESULT& reason) const = 0;

    // "Direct3D 12, 1920x1080, format 28, 3 buffers, queue found in the swap chain" for the log.
    virtual std::string Details() const = 0;

    IDXGISwapChain* SwapChain() const { return swapChain_; }
    HWND Window() const { return window_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

    // Re-reads the back buffer size (every frame).
    void UpdateSize();

protected:
    explicit Renderer(IDXGISwapChain* swapChain);

    IDXGISwapChain* swapChain_;  // not owned: the game owns the swap chain
    HWND window_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
};

// Picks Direct3D 11 or 12 from the swap chain's device. A Direct3D 12 renderer draws on the game's
// command queue: the one the swap chain was created with if it is among `queues` (the game's direct
// queues seen so far), otherwise `lastQueue`. While no queue of the swap chain's device is known,
// this returns null with `retry` set.
std::unique_ptr<Renderer> CreateRenderer(IDXGISwapChain* swapChain, const std::vector<ID3D12CommandQueue*>& queues,
                                         ID3D12CommandQueue* lastQueue, bool& retry, std::string& error);

}  // namespace gui
