#include "engine/core/Application.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#if FW_PLATFORM_WINDOWS
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <stb_image_write.h>
#include <filesystem>
#include <cstring>

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
    ImDrawData* drawData = ImGui::GetDrawData();
    int cmdLists = 0, vertices = 0, indices = 0;
    if (drawData) {
        cmdLists = drawData->CmdListsCount;
        vertices = drawData->TotalVtxCount;
        indices = drawData->TotalIdxCount;
    }
    const int frameW = m_SoftwareMode ? m_Canvas.Width() : m_Window.Width();
    const int frameH = m_SoftwareMode ? m_Canvas.Height() : m_Window.Height();

    FW_LOG_INFO("Startup report: frame %d | mode %s | window %dx%d | frame %dx%d | ImGui display %.0fx%.0f | "
                "draw lists %d (%d verts, %d indices) | renderer %s",
                m_FrameIndex, m_SoftwareMode ? "CPU (GDI)" : "GPU (D3D11)",
                m_Window.Width(), m_Window.Height(), frameW, frameH,
                ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y,
                cmdLists, vertices, indices,
                m_SoftwareMode ? "CPU canvas" : (m_Renderer ? "created" : "MISSING"));

    if (cmdLists == 0 || vertices == 0)
        FW_LOG_ERROR("ImGui produced no draw data - the window will look empty. "
                     "Check that ImGui was initialized (ImGui_ImplWin32_Init / renderer backend).");
    if (m_Window.Width() <= 0 || m_Window.Height() <= 0)
        FW_LOG_ERROR("Window client size is %dx%d - the window is minimized or was created with a bad size.",
                     m_Window.Width(), m_Window.Height());
}

bool Application::RunInternal(const std::string& title, int width, int height, bool maximized) {
    OpenLogFileIfNeeded();
    FW_LOG_INFO("=== Forgeworks starting: %s (%dx%d, maximized=%d, software=%d) ===",
                title.c_str(), width, height, maximized ? 1 : 0, m_ForceSoftware ? 1 : 0);

    WindowDesc desc;
    desc.title = title;
    desc.width = width;
    desc.height = height;
    desc.maximized = maximized;
    if (!m_Window.Create(desc)) {
        FW_LOG_ERROR("Window creation failed - nothing can be shown. See %s",
                     Log::Get().FilePath().c_str());
        return false;
    }

    m_Window.OnRawMessage = [](void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam) -> bool {
        return ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wParam, (LPARAM)lParam) != 0;
    };
    m_Window.OnResize = [this](int w, int h) {
        if (w <= 0 || h <= 0) return;
        if (m_SoftwareMode) m_Canvas.Resize(w, h);
        else m_Device.Resize(w, h);
        if (m_Renderer) m_Renderer->OnResize(w, h);
        OnResize(w, h);
    };

    // ---- presentation path selection ---------------------------------------
    // The GPU path is preferred, but *every* failure below drops us onto the CPU
    // path instead of returning: the editor must always end up showing
    // something. --software skips the GPU entirely.
    bool gpuReady = false;
    if (m_ForceSoftware) {
        FW_LOG_INFO("Software mode requested: the D3D11 device is not created "
                    "(the whole frame is rendered on the CPU and blitted with GDI)");
    } else if (!m_Device.Init(m_Window, true)) {
        FW_LOG_ERROR("D3D11 device/swap chain creation failed (hr above). "
                     "Falling back to the CPU presentation path.");
    } else {
        // The window may have been resized (maximize) before the device existed;
        // keep the swap chain in sync with the real client area.
        if (m_Window.Width() != m_Device.Width() || m_Window.Height() != m_Device.Height()) {
            FW_LOG_INFO("Syncing swap chain to window size %dx%d", m_Window.Width(), m_Window.Height());
            m_Device.Resize(m_Window.Width(), m_Window.Height());
        }

        m_Renderer = MakeScope<Renderer>(&m_Device);
        if (!m_Renderer->Init()) {
            FW_LOG_ERROR("Renderer initialization failed - falling back to the CPU presentation path.");
            m_Renderer.reset();
            m_Device.Shutdown();
        } else {
            gpuReady = true;
        }
    }

    if (!gpuReady) {
        m_SoftwareMode = true;
        m_Canvas.Resize(m_Window.Width(), m_Window.Height());
        FW_LOG_INFO("CPU presentation path active: %dx%d canvas, %s",
                    m_Canvas.Width(), m_Canvas.Height(),
                    m_Window.HasSoftwareFrame() ? "GDI blit" : "GDI blit (first frame pending)");
    }

    if (!InitImGui(gpuReady)) {
        // The ImGui DX11 backend could not start: recreate the context on the CPU
        // path rather than leaving an empty window.
        FW_LOG_WARN("The ImGui GPU backend is unavailable - switching to the CPU presentation path");
        ShutdownImGui();
        m_SoftwareMode = true;
        m_Canvas.Resize(m_Window.Width(), m_Window.Height());
        if (!InitImGui(false)) {
            FW_LOG_ERROR("Could not initialize ImGui at all - nothing can be drawn.");
            return false;
        }
    }

    const bool started = OnInit();
    if (m_SoftwareMode)
        m_Window.SetTitle(title + "  [CPU rendering]");

    if (!started) {
        FW_LOG_ERROR("Application::OnInit() returned false. Showing the diagnostics screen "
                     "(reason + log) inside the window instead of leaving it blank.");
        m_FailureReason = "OnInit() failed - see the log below";
        OnShutdown();  // release whatever the app managed to create
        SwitchToSoftware("OnInit() failed");
        m_FailureScreen = true;
    }

    MainLoop();

    if (started) OnShutdown();
    ShutdownImGui();
    return started;
}

void Application::SwitchToSoftware(const std::string& reason) {
    if (m_SoftwareMode) return;
    ShutdownImGui();
    m_Renderer.reset();
    m_Device.Shutdown();
    m_SoftwareMode = true;
    m_Canvas.Resize(m_Window.Width() > 0 ? m_Window.Width() : 1280,
                    m_Window.Height() > 0 ? m_Window.Height() : 720);
    InitImGui(false);
    FW_LOG_WARN("Switched to the CPU presentation path (%s) - the window stays usable without D3D11",
                reason.c_str());
}

bool Application::InitImGui(bool useDx11) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Multi-viewport windows are a DX11-backend feature (they create real OS
    // windows); the CPU canvas draws a single window, so leave them off there.
    if (useDx11) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    if (ImGui_ImplWin32_Init(m_Window.Handle())) {
        m_PlatformBackendReady = true;
    } else {
        FW_LOG_ERROR("ImGui_ImplWin32_Init failed - mouse/keyboard input will not reach the editor. "
                     "Setting a manual display size so the UI can still be drawn.");
    }

    if (useDx11) {
        if (!ImGui_ImplDX11_Init(m_Device.Device(), m_Device.Context())) {
            FW_LOG_ERROR("ImGui_ImplDX11_Init failed (device/context unusable)");
            return false;
        }
        m_ImGuiBackend = ImGuiBackend::DX11;
    } else {
        // Tell ImGui that our renderer creates/updates the font atlas texture
        // itself: engine/render/SoftCanvas.cpp honours those requests.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendRendererName = "forgeworks_softcanvas";
        io.BackendPlatformName = "forgeworks_win32";
        m_ImGuiBackend = ImGuiBackend::Software;
    }
    FW_LOG_INFO("ImGui initialized (%s backend)", useDx11 ? "D3D11" : "CPU canvas");
    return true;
}

void Application::ShutdownImGui() {
    if (ImGui::GetCurrentContext()) {
        if (m_ImGuiBackend == ImGuiBackend::DX11) ImGui_ImplDX11_Shutdown();
        if (m_PlatformBackendReady) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    m_ImGuiBackend = ImGuiBackend::None;
    m_PlatformBackendReady = false;
}

void Application::NewImGuiFrame() {
    if (m_ImGuiBackend == ImGuiBackend::DX11) ImGui_ImplDX11_NewFrame();
    if (m_PlatformBackendReady) {
        ImGui_ImplWin32_NewFrame();
    } else {
        // No platform backend (or it failed): keep the UI rendering anyway.
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)(m_SoftwareMode ? m_Canvas.Width() : m_Window.Width()),
                                (float)(m_SoftwareMode ? m_Canvas.Height() : m_Window.Height()));
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
    }
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

// Last-resort UI: shown inside the window when the application could not start
// its own content, so the failure can be read on screen instead of guessed at.
void Application::DrawFailureScreen() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Forgeworks diagnostics", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
    ImGui::TextWrapped("Forgeworks could not start its content.");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", m_FailureReason.c_str());
    ImGui::Separator();

    ImGui::Text("Presentation : %s", "CPU canvas (GDI) - no GPU involved");
    ImGui::Text("Project root : %s", Paths::ProjectRoot().c_str());
    ImGui::Text("Log file     : %s", Log::Get().FilePath().c_str());
    ImGui::Text("Window       : %dx%d", m_Window.Width(), m_Window.Height());
    ImGui::Separator();

    ImGui::TextDisabled("Recent log (the whole log is in the file above):");
    ImGui::BeginChild("log", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8.0f), true);
    const std::vector<LogEntry> entries = Log::Get().Snapshot();
    const size_t first = entries.size() > 40 ? entries.size() - 40 : 0;
    for (size_t i = first; i < entries.size(); i++) {
        const char* level = Log::LevelName(entries[i].level);
        const ImVec4 color = entries[i].level == LogLevel::Error   ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f)
                             : entries[i].level == LogLevel::Warn  ? ImVec4(0.95f, 0.80f, 0.40f, 1.0f)
                                                                   : ImVec4(0.80f, 0.80f, 0.84f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s %s", level, entries[i].message.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    if (ImGui::Button("Quit") || ImGui::IsKeyPressed(ImGuiKey_Escape)) m_Running = false;
    ImGui::SameLine();
    ImGui::TextDisabled("Run the editor with --console to watch this log live.");

    ImGui::End();
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

        if (!m_FailureScreen) OnUpdate(dt);

        const float clearColor[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
        if (m_SoftwareMode) {
            m_Canvas.Clear(IM_COL32(13, 13, 16, 255));
            if (!m_FailureScreen) OnSoftwareRender(m_Canvas);
        } else {
            m_Device.BeginFrame(clearColor);
            OnRender();
        }

        NewImGuiFrame();
        if (m_FailureScreen) DrawFailureScreen();
        else OnImGui();

        if (m_SoftwareMode) {
            // Rasterizes the ImGui interface (paint + font textures) into the canvas.
            m_Canvas.RenderImGuiFrame();
        } else {
            RenderImGui();
        }

        m_FrameIndex++;
        if (!m_StartupReported && m_StartupReportFrame > 0 && m_FrameIndex >= m_StartupReportFrame) {
            m_StartupReported = true;
            LogStartupReport();
        }
        // Screenshot mode: capture the fully drawn frame (scene + every ImGui
        // panel) right before presenting, then quit.
        if (!m_ScreenshotPath.empty() && m_FrameIndex >= m_ScreenshotFrames) {
            CaptureFrameToPng(m_ScreenshotPath);
            m_Running = false;
            break;
        }

        if (m_SoftwareMode) {
            m_Window.BlitSoftwareFrame(m_Canvas.Pixels(), m_Canvas.Width(), m_Canvas.Height());
        } else {
            m_Device.Present();
        }
    }

    FW_LOG_INFO("=== Forgeworks shutting down after %d frame(s) (%s) ===",
                m_FrameIndex, m_SoftwareMode ? "CPU presentation" : "GPU presentation");
}

bool Application::CaptureFrameToPng(const std::string& path) {
    return m_SoftwareMode ? CaptureCanvasToPng(path) : CaptureBackBufferToPng(path);
}

bool Application::CaptureCanvasToPng(const std::string& path) {
    if (m_Canvas.Empty()) {
        FW_LOG_ERROR("Screenshot: the CPU canvas is empty");
        return false;
    }
    const std::string resolved = Paths::Resolve(path);
    std::error_code ec;
    if (std::filesystem::path(resolved).has_parent_path())
        std::filesystem::create_directories(std::filesystem::path(resolved).parent_path(), ec);

    if (!stbi_write_png(resolved.c_str(), m_Canvas.Width(), m_Canvas.Height(), 4,
                        m_Canvas.Pixels(), m_Canvas.Pitch())) {
        FW_LOG_ERROR("Screenshot: failed to write %s", resolved.c_str());
        return false;
    }
    FW_LOG_INFO("Window screenshot saved: %s (%dx%d, CPU canvas)", resolved.c_str(),
                m_Canvas.Width(), m_Canvas.Height());
    return true;
}

bool Application::CaptureBackBufferToPng(const std::string& path) {
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
}

} // namespace fw

#endif
