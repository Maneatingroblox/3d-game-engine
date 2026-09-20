#include "engine/core/Application.h"
#include "engine/core/Log.h"

#if FW_PLATFORM_WINDOWS
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fw {

bool Application::Run(const std::string& title, int width, int height, bool maximized) {
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

        m_Device.Present();
    }
}

} // namespace fw

#endif
