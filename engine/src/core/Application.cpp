#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#if FW_PLATFORM_WINDOWS
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <stb_image_write.h>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fw {

bool Application::Run(const std::string& title, int width, int height, bool maximized) {
    m_ScreenshotPath.clear();
    m_ScreenshotFrames = 0;
    m_FrameIndex = 0;
    return RunInternal(title, width, height, maximized);
}

bool Application::RunWithScreenshot(const std::string& title, int width, int height,
                                    const std::string& screenshotPath, int frames, bool maximized) {
    m_ScreenshotPath = screenshotPath;
    m_ScreenshotFrames = frames > 0 ? frames : 1;
    m_FrameIndex = 0;
    const bool ok = RunInternal(title, width, height, maximized);
    OnScreenshot(screenshotPath);
    return ok;
}

bool Application::RunInternal(const std::string& title, int width, int height, bool maximized) {
    WindowDesc desc;
    desc.title = title;
    desc.width = width;
    desc.height = height;
    desc.maximized = maximized;
    if (!m_Window.Create(desc)) return false;

    m_Window.OnRawMessage = [](void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam) -> bool {
        return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wParam, (LPARAM)lParam) != 0;
    };
    m_Window.OnResize = [this](int w, int h) {
        if (w <= 0 || h <= 0) return;
        m_Device.Resize(w, h);
        if (m_Renderer) m_Renderer->OnResize(w, h);
        OnResize(w, h);
    };

    if (!m_Device.Init(m_Window, true)) return false;

    m_Renderer = MakeScope<Renderer>(&m_Device);
    if (!m_Renderer->Init()) return false;

    InitImGui();

    if (!OnInit()) return false;

    MainLoop();

    OnShutdown();
    ShutdownImGui();
    return true;
}

bool Application::CaptureWindowToPng(const std::string& path) {
#if FW_PLATFORM_WINDOWS
    if (!m_Device.SwapChain() || !m_Device.Device() || !m_Device.Context()) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_Device.SwapChain()->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        FW_LOG_ERROR("Screenshot: could not access the back buffer");
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MipLevels = 1;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_Device.Device()->CreateTexture2D(&desc, nullptr, &staging))) {
        FW_LOG_ERROR("Screenshot: could not create the read-back texture");
        return false;
    }

    m_Device.Context()->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_Device.Context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        FW_LOG_ERROR("Screenshot: could not map the read-back texture");
        return false;
    }

    std::vector<u8> pixels((size_t)desc.Width * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; y++) {
        const u8* src = (const u8*)mapped.pData + (size_t)y * mapped.RowPitch;
        std::memcpy(pixels.data() + (size_t)y * desc.Width * 4, src, (size_t)desc.Width * 4);
    }
    m_Device.Context()->Unmap(staging.Get(), 0);

    const std::string resolved = Paths::Resolve(path);
    std::error_code ec;
    if (std::filesystem::path(resolved).has_parent_path())
        std::filesystem::create_directories(std::filesystem::path(resolved).parent_path(), ec);

    if (!stbi_write_png(resolved.c_str(), (int)desc.Width, (int)desc.Height, 4, pixels.data(), (int)desc.Width * 4)) {
        FW_LOG_ERROR("Screenshot: failed to write %s", resolved.c_str());
        return false;
    }
    FW_LOG_INFO("Window screenshot saved: %s (%ux%u)", resolved.c_str(), desc.Width, desc.Height);
    return true;
#else
    FW_UNUSED(path);
    return false;
#endif
}

void Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_Window.Handle());
    ImGui_ImplDX11_Init(m_Device.Device(), m_Device.Context());
}

void Application::ShutdownImGui() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void Application::NewImGuiFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Application::RenderImGui() {
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void Application::MainLoop() {
    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();

    while (m_Running) {
        if (!m_Window.PumpMessages()) { m_Running = false; break; }
        if (m_Window.IsMinimized()) continue;

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::min(dt, 0.1f); // clamp to avoid huge steps after a stall/breakpoint

        Input::Get().NewFrame();

        OnUpdate(dt);

        const float clearColor[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
        m_Device.BeginFrame(clearColor);
        OnRender();

        NewImGuiFrame();
        OnImGui();
        RenderImGui();

        m_FrameIndex++;
        // Screenshot mode: capture the fully drawn window (scene + every ImGui
        // panel) right before presenting, then quit.
        if (!m_ScreenshotPath.empty() && m_FrameIndex >= m_ScreenshotFrames) {
            CaptureWindowToPng(m_ScreenshotPath);
            m_Running = false;
            break;
        }

        m_Device.Present();
    }
}

} // namespace fw

#endif
