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
    void MainLoop();
    void InitImGui();
    void ShutdownImGui();
    void NewImGuiFrame();
    void RenderImGui();
};

} // namespace fw
