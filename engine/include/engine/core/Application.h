#pragma once
// Shared application scaffolding: Win32 window + D3D11 device + Dear ImGui
// bootstrap + main loop timing. Both ForgeworksEditor (the Map Maker) and
// ForgeworksGame (the standalone runtime) derive from this so window/device/
// ImGui setup isn't duplicated between them.

#include "engine/core/Base.h"
#include "engine/platform/Window.h"
#include "engine/render/RenderDevice.h"
#include "engine/render/Renderer.h"
#include "engine/platform/Input.h"
#include <chrono>
#include <string>

namespace fw {

class Application {
public:
    Application() = default;
    virtual ~Application() = default;

    // Creates the window + D3D11 device + ImGui context, then calls OnInit().
    bool Run(const std::string& title, int width, int height, bool maximized = false);

    // Same as Run(), but after `frames` frames the back buffer is copied to a
    // PNG (`screenshotPath`) and the app exits. Used to capture what the editor
    // window actually looks like (including all ImGui panels) from a build
    // machine or CI, where nobody can look at the screen. See
    // editor/src/main.cpp for the --screenshot command line switch.
    bool RunWithScreenshot(const std::string& title, int width, int height,
                           const std::string& screenshotPath, int frames = 12,
                           bool maximized = false);

    // Path passed to RunWithScreenshot(); empty during normal runs. Virtual so
    // subclasses (the editor) can also capture their own off-screen viewport.
    const std::string& ScreenshotPath() const { return m_ScreenshotPath; }

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
    virtual void OnImGui() {}
    virtual void OnShutdown() {}
    virtual void OnResize(int width, int height) { FW_UNUSED(width); FW_UNUSED(height); }

    Window m_Window;
    RenderDevice m_Device;
    Scope<Renderer> m_Renderer;
    bool m_Running = true;

private:
    bool RunInternal(const std::string& title, int width, int height, bool maximized);
    void MainLoop();
    void InitImGui();
    void ShutdownImGui();
    void NewImGuiFrame();
    void RenderImGui();
    // Reads the swap chain back buffer into `path` as a PNG.
    bool CaptureWindowToPng(const std::string& path);

    std::string m_ScreenshotPath;
    int m_ScreenshotFrames = 0;
    int m_FrameIndex = 0;

    std::string m_LogFile;
    bool m_ConsoleOutput = false;
    int m_StartupReportFrame = 5;
    bool m_StartupReported = false;
    void OpenLogFileIfNeeded();
    void LogStartupReport();
};

} // namespace fw
