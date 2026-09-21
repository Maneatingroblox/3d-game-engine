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

void Application::OpenLogFileIfNeeded() {
#if FW_PLATFORM_WINDOWS
    if (m_ConsoleOutput) {
        if (AllocConsole()) {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
            SetConsoleTitleW(L"Forgeworks log");
            FW_LOG_INFO("Console attached (--console)");
        } else {
            m_ConsoleOutput = false;
        }
    }
#endif

    std::string path = m_LogFile;
    if (path.empty() && !Paths::ExecutableDir().empty())
        path = (std::filesystem::path(Paths::ExecutableDir()) / "forgeworks.log").string();
    if (path.empty()) path = "forgeworks.log";

    Log::Get().SetConsoleEcho(true);
    Log::Get().SetFile(path);
    FW_LOG_INFO("Log file: %s", Paths::Resolve(path).c_str());
}

void Application::LogStartupReport() {
#if FW_PLATFORM_WINDOWS
    ImDrawData* drawData = ImGui::GetDrawData();
    int cmdLists = 0, vertices = 0, indices = 0;
    if (drawData) {
        cmdLists = drawData->CmdListsCount;
        vertices = drawData->TotalVtxCount;
        indices = drawData->TotalIdxCount;
    }
    FW_LOG_INFO("Startup report: frame %d | window %dx%d | swap chain %dx%d | ImGui display %.0fx%.0f | "
                "draw lists %d (%d verts, %d indices) | renderer %s",
                m_FrameIndex, m_Window.Width(), m_Window.Height(), m_Device.Width(), m_Device.Height(),
                ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y,
                cmdLists, vertices, indices, m_Renderer ? "created" : "MISSING");

    if (cmdLists == 0 || vertices == 0)
        FW_LOG_ERROR("ImGui produced no draw data - the window will look empty. "
                     "Check that ImGui was initialized (ImGui_ImplWin32_Init / ImGui_ImplDX11_Init).");
    if (m_Window.Width() <= 0 || m_Window.Height() <= 0)
        FW_LOG_ERROR("Window client size is %dx%d - the window is minimized or was created with a bad size.",
                     m_Window.Width(), m_Window.Height());
#else
    FW_LOG_INFO("Startup report: frame %d", m_FrameIndex);
#endif
}

bool Application::RunInternal(const std::string& title, int width, int height, bool maximized) {
    OpenLogFileIfNeeded();
    FW_LOG_INFO("=== Forgeworks starting: %s (%dx%d, maximized=%d) ===", title.c_str(), width, height, maximized ? 1 : 0);

    WindowDesc desc;
    desc.title = title;
    desc.width = width;
    desc.height = height;
    desc.maximized = maximized;
    if (!m_Window.Create(desc)) {
        FW_LOG_ERROR("Window creation failed - nothing can be shown.");
        return false;
    }

    m_Window.OnRawMessage = [](void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam) -> bool {
        return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wParam, (LPARAM)lParam) != 0;
    };
    m_Window.OnResize = [this](int w, int h) {
        if (w <= 0 || h <= 0) return;
        m_Device.Resize(w, h);
        if (m_Renderer) m_Renderer->OnResize(w, h);
        OnResize(w, h);
    };

    if (!m_Device.Init(m_Window, true)) {
        FW_LOG_ERROR("D3D11 device/swap chain creation failed - the window will stay blank. "
                     "Update the GPU driver, or run with --screenshot to capture diagnostics.");
        return false;
    }
    // The window may have been resized (maximize) before the device existed;
    // keep the swap chain in sync with the real client area.
    if (m_Window.Width() != m_Device.Width() || m_Window.Height() != m_Device.Height()) {
        FW_LOG_INFO("Syncing swap chain to window size %dx%d", m_Window.Width(), m_Window.Height());
        m_Device.Resize(m_Window.Width(), m_Window.Height());
    }

    m_Renderer = MakeScope<Renderer>(&m_Device);
    if (!m_Renderer->Init()) {
        FW_LOG_ERROR("Renderer initialization failed - the window will stay blank.");
        return false;
    }

    InitImGui();

    if (!OnInit()) {
        FW_LOG_ERROR("Application::OnInit() returned false - shutting down before the main loop.");
        return false;
    }

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
        if (!m_StartupReported && m_StartupReportFrame > 0 && m_FrameIndex >= m_StartupReportFrame) {
            m_StartupReported = true;
            LogStartupReport();
        }
        // Screenshot mode: capture the fully drawn window (scene + every ImGui
        // panel) right before presenting, then quit.
        if (!m_ScreenshotPath.empty() && m_FrameIndex >= m_ScreenshotFrames) {
            CaptureWindowToPng(m_ScreenshotPath);
            m_Running = false;
            break;
        }

        m_Device.Present();
    }

    FW_LOG_INFO("=== Forgeworks shutting down after %d frame(s) ===", m_FrameIndex);
}

} // namespace fw

#endif
