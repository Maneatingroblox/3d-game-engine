#pragma once
// Shared application scaffolding: Win32 window + D3D11 device + Dear ImGui
// bootstrap + main loop timing. Both ForgeworksEditor (the Map Maker) and
// ForgeworksGame (the standalone runtime) derive from this so window/device/
// ImGui setup isn't duplicated between them.
//
// There are two presentation paths:
//   * GPU  - D3D11 swap chain + the ImGui DX11 backend (the normal case).
//   * CPU  - engine/render/SoftCanvas rasterizes the whole frame (app UI and
//            whatever the subclass draws in OnSoftwareRender) and the Win32
//            window blits it with GDI. This is used when --software is passed
//            and, more importantly, as an automatic fallback whenever the D3D11
//            device, the renderer or a shader is unavailable - so a broken GPU
//            path can no longer end in a blank window.

#include "engine/core/Base.h"
#include "engine/platform/Window.h"
#include "engine/render/RenderDevice.h"
#include "engine/render/SoftCanvas.h"
#include "engine/platform/Input.h"
#include <chrono>
#include <string>

#if FW_PLATFORM_WINDOWS
// The D3D11 scene renderer only exists on Windows; hosts without it (headless
// validation, tools/fwui) run on engine/render/SoftCanvas.h instead.
#include "engine/render/Renderer.h"
#endif

namespace fw {

class Application {
public:
    Application() = default;
    virtual ~Application() = default;

    // Creates the window + D3D11 device + ImGui context, then calls OnInit().
    bool Run(const std::string& title, int width, int height, bool maximized = false);

    // Same as Run(), but after `frames` frames the frame is copied to a PNG
    // (`screenshotPath`) and the app exits. Used to capture what the editor
    // window actually looks like (including all ImGui panels) from a build
    // machine or CI, where nobody can look at the screen. See
    // editor/src/main.cpp for the --screenshot command line switch.
    bool RunWithScreenshot(const std::string& title, int width, int height,
                           const std::string& screenshotPath, int frames = 12,
                           bool maximized = false);

    // Path passed to RunWithScreenshot(); empty during normal runs. Virtual so
    // subclasses (the editor) can also capture their own off-screen viewport.
    const std::string& ScreenshotPath() const { return m_ScreenshotPath; }

    // ---- presentation mode --------------------------------------------------
    // --software: never create the D3D11 device; render everything on the CPU
    // and present through GDI. Useful on machines with a broken/absent GPU or
    // driver, in VMs and over remote desktop.
    void SetSoftwareMode(bool force) { m_ForceSoftware = force; m_SoftwareMode = force; }
    // True once the app is actually running on the CPU presentation path -
    // either because --software was passed or because the GPU path failed.
    bool SoftwareMode() const { return m_SoftwareMode; }
    SoftCanvas& Canvas() { return m_Canvas; }
    const SoftCanvas& Canvas() const { return m_Canvas; }

    // ---- startup diagnostics -------------------------------------------------
    // Where the log is written. Defaults to "<dir of the executable>/forgeworks.log"
    // because a Windows GUI app has no console: without a log file there is no
    // way to tell what happened on a user's machine.
    void SetLogFile(const std::string& path) { m_LogFile = path; }
    const std::string& LogFile() const { return m_LogFile; }
    // Allocates a console window and echoes the log to it (--console).
    void SetConsoleOutput(bool enabled) { m_ConsoleOutput = enabled; }
    // Number of frames after which a one-line "did anything render?" report is
    // logged (window size, swap chain, ImGui draw data). 0 disables it.
    void SetStartupReportFrame(int frame) { m_StartupReportFrame = frame; }

    int FrameIndex() const { return m_FrameIndex; }

protected:
    // Called just before the app exits in RunWithScreenshot() mode, after the
    // window image has been written. Lets a subclass save extra images.
    virtual void OnScreenshot(const std::string& path) { FW_UNUSED(path); }

protected:
    virtual bool OnInit() { return true; }
    virtual void OnUpdate(float dt) { FW_UNUSED(dt); }
    virtual void OnRender() {}
    // Called instead of OnRender() when running on the CPU presentation path
    // (the ImGui interface is drawn after this). The editor has nothing to do
    // here - its 3D viewport is an ImGui image - but a game would rasterize its
    // scene into `canvas` here.
    virtual void OnSoftwareRender(SoftCanvas& canvas) { FW_UNUSED(canvas); }
    virtual void OnImGui() {}
    virtual void OnShutdown() {}
    virtual void OnResize(int width, int height) { FW_UNUSED(width); FW_UNUSED(height); }

    Window m_Window;
    RenderDevice m_Device;
#if FW_PLATFORM_WINDOWS
    Scope<Renderer> m_Renderer;
#endif
    bool m_Running = true;

private:
    enum class ImGuiBackend { None, DX11, Software };

    bool RunInternal(const std::string& title, int width, int height, bool maximized);
    void MainLoop();
    bool InitImGui(bool useDx11);
    void ShutdownImGui();
    void NewImGuiFrame();
    void RenderImGui();
    // Drops the GPU path and continues on the CPU canvas (keeps the window alive
    // instead of exiting into a blank screen).
    void SwitchToSoftware(const std::string& reason);
    // Shown instead of the app's own UI when OnInit() failed: the reason, the
    // environment and the log tail, inside the window. A start-up failure is
    // now something the user can read, never an empty window.
    void DrawFailureScreen();
    // Reads the current frame (swap chain back buffer or canvas) into `path`.
    bool CaptureFrameToPng(const std::string& path);
    bool CaptureBackBufferToPng(const std::string& path);
    bool CaptureCanvasToPng(const std::string& path);

    std::string m_ScreenshotPath;
    int m_ScreenshotFrames = 0;
    int m_FrameIndex = 0;

    std::string m_LogFile;
    bool m_ConsoleOutput = false;
    int m_StartupReportFrame = 5;
    bool m_StartupReported = false;
    void OpenLogFileIfNeeded();
    void LogStartupReport();

    bool m_ForceSoftware = false;
    bool m_SoftwareMode = false;
    bool m_PlatformBackendReady = false;
    bool m_FailureScreen = false;
    std::string m_FailureReason;
    ImGuiBackend m_ImGuiBackend = ImGuiBackend::None;
    SoftCanvas m_Canvas;
};

} // namespace fw
