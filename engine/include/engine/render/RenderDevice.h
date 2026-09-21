#pragma once
// Owns the D3D11 device/context/swapchain and the main back-buffer render
// target + depth buffer. Everything else in engine/render (Renderer,
// ShaderLibrary, MaterialSystem, GpuMeshCache) talks to the device through
// this class.
//
// On hosts without D3D11 (headless validation, tools/ui_shot) the device is a
// stub that reports failure instead of linking errors, so the editor's UI can
// still be exercised through the CPU path (engine/render/SoftCanvas.h).

#include "engine/core/Base.h"

#if FW_PLATFORM_WINDOWS
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace fw {

class Window;

#if FW_PLATFORM_WINDOWS

class RenderDevice {
public:
    RenderDevice();
    ~RenderDevice();

    bool Init(Window& window, bool vsync);
    void Shutdown();

    void Resize(int width, int height);
    void BeginFrame(const float clearColor[4]);
    // Binds the swap-chain back buffer (RTV + DSV) and sets the full-size
    // viewport, without clearing. Call this after rendering into an off-screen
    // target (the editor's viewport) and before drawing anything that must end
    // up in the window - including ImGui, whose D3D11 backend renders into the
    // currently bound target instead of setting one.
    void BindBackBufferTargets();
    void Present();

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    void SetVSync(bool vsync) { m_VSync = vsync; }
    // True when the GPU device was lost (driver reset/removal) - the app can
    // then tell the user instead of showing a frozen window.
    bool DeviceLost() const { return m_DeviceLost; }

    ID3D11Device* Device() const { return m_Device.Get(); }
    ID3D11DeviceContext* Context() const { return m_Context.Get(); }
    IDXGISwapChain* SwapChain() const { return m_SwapChain.Get(); }
    ID3D11RenderTargetView* BackBufferRTV() const { return m_BackBufferRTV.Get(); }
    ID3D11DepthStencilView* DepthStencilView() const { return m_DepthStencilView.Get(); }
    ID3D11ShaderResourceView* DepthSRV() const { return m_DepthSRV.Get(); }

private:
    void CreateSizeDependentResources(int width, int height);

    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_Context;
    ComPtr<IDXGISwapChain> m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_BackBufferRTV;
    ComPtr<ID3D11Texture2D> m_DepthStencilBuffer;
    ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
    ComPtr<ID3D11ShaderResourceView> m_DepthSRV;
    HWND m_Hwnd = nullptr;
    int m_Width = 0, m_Height = 0;
    bool m_VSync = true;
    bool m_DeviceLost = false;
    bool m_PresentFailed = false;
};

#else // !FW_PLATFORM_WINDOWS

class RenderDevice {
public:
    RenderDevice() = default;
    ~RenderDevice() = default;

    bool Init(Window&, bool) { return false; }  // no GPU path on this host
    void Shutdown() {}
    void Resize(int, int) {}
    void BeginFrame(const float[4]) {}
    void Present() {}

    int Width() const { return 0; }
    int Height() const { return 0; }
    void SetVSync(bool) {}
    bool DeviceLost() const { return false; }
};

#endif // FW_PLATFORM_WINDOWS

} // namespace fw
