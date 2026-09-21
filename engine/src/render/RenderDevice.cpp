#include "engine/render/RenderDevice.h"
#include "engine/platform/Window.h"
#include "engine/core/Log.h"

#if FW_PLATFORM_WINDOWS
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace fw {

RenderDevice::RenderDevice() = default;
RenderDevice::~RenderDevice() { Shutdown(); }

bool RenderDevice::Init(Window& window, bool vsync) {
    m_Hwnd = window.Handle();
    m_VSync = vsync;
    m_Width = window.Width();
    m_Height = window.Height();

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = m_Width;
    scd.BufferDesc.Height = m_Height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = m_Hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createFlags = 0;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtained;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        requestedLevels, ARRAYSIZE(requestedLevels), D3D11_SDK_VERSION,
        &scd, &m_SwapChain, &m_Device, &obtained, &m_Context);

#if defined(_DEBUG)
    if (FAILED(hr) && (createFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        // The DirectX debug layer is an optional Windows feature; without it,
        // requesting it makes device creation fail outright. Retry without it
        // instead of leaving the user with a blank window.
        FW_LOG_WARN("D3D11 device creation with the debug layer failed (hr=0x%08lX) - "
                    "retrying without D3D11_CREATE_DEVICE_DEBUG", hr);
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
            requestedLevels, ARRAYSIZE(requestedLevels), D3D11_SDK_VERSION,
            &scd, &m_SwapChain, &m_Device, &obtained, &m_Context);
    }
#endif

    if (FAILED(hr)) {
        FW_LOG_ERROR("D3D11CreateDeviceAndSwapChain failed (hr=0x%08lX) - no rendering is possible", hr);
        // A missing GPU/driver is fatal for the GPU path; leave a clear marker
        // in the log so it isn't mistaken for "the editor shows nothing".
        return false;
    }

    CreateSizeDependentResources(m_Width, m_Height);
    FW_LOG_INFO("D3D11 render device initialized (%dx%d, feature level %d)", m_Width, m_Height, (int)obtained);
    return true;
}

void RenderDevice::CreateSizeDependentResources(int width, int height) {
    m_BackBufferRTV.Reset();
    m_DepthStencilView.Reset();
    m_DepthSRV.Reset();
    m_DepthStencilBuffer.Reset();

    ComPtr<ID3D11Texture2D> backBuffer;
    m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_BackBufferRTV);

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    m_Device->CreateTexture2D(&depthDesc, nullptr, &m_DepthStencilBuffer);

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, &m_DepthStencilView);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_Device->CreateShaderResourceView(m_DepthStencilBuffer.Get(), &srvDesc, &m_DepthSRV);

    m_Width = width;
    m_Height = height;
}

void RenderDevice::Resize(int width, int height) {
    if (width <= 0 || height <= 0 || !m_SwapChain) return;
    m_BackBufferRTV.Reset();
    m_DepthStencilView.Reset();
    m_DepthSRV.Reset();
    m_DepthStencilBuffer.Reset();
    m_Context->OMSetRenderTargets(0, nullptr, nullptr);
    m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateSizeDependentResources(width, height);
}

void RenderDevice::BeginFrame(const float clearColor[4]) {
    m_Context->OMSetRenderTargets(1, m_BackBufferRTV.GetAddressOf(), m_DepthStencilView.Get());
    m_Context->ClearRenderTargetView(m_BackBufferRTV.Get(), clearColor);
    m_Context->ClearDepthStencilView(m_DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_Width;
    vp.Height = (float)m_Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_Context->RSSetViewports(1, &vp);
}

void RenderDevice::Present() {
    if (!m_SwapChain) return;

    const HRESULT hr = m_SwapChain->Present(m_VSync ? 1 : 0, 0);
    if (hr == DXGI_STATUS_OCCLUDED) return; // window hidden; nothing to do

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        const HRESULT reason = m_Device ? m_Device->GetDeviceRemovedReason() : hr;
        FW_LOG_ERROR("D3D11 device lost during Present (hr=0x%08lX, reason=0x%08lX) - "
                     "the window will stop updating.", hr, reason);
        m_DeviceLost = true;
        return;
    }
    if (FAILED(hr)) {
        // Report once, not every frame.
        if (!m_PresentFailed) {
            m_PresentFailed = true;
            FW_LOG_ERROR("Swap chain Present() failed (hr=0x%08lX) - the window may stay blank", hr);
        }
    }
}

void RenderDevice::Shutdown() {
    m_DepthSRV.Reset();
    m_DepthStencilView.Reset();
    m_DepthStencilBuffer.Reset();
    m_BackBufferRTV.Reset();
    m_SwapChain.Reset();
    m_Context.Reset();
    m_Device.Reset();
}

} // namespace fw

#endif // FW_PLATFORM_WINDOWS
