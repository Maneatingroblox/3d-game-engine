#pragma once
// Win32 window wrapper. Owns the HWND, pumps the message loop, and forwards
// input/resize events to whoever registered callbacks (the editor's ImGui
// backend, and engine/platform/Input).

#include "engine/core/Base.h"
#include <string>
#include <functional>

#if FW_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace fw {

struct WindowDesc {
    std::string title = "Forgeworks";
    int width = 1600;
    int height = 900;
    bool resizable = true;
    bool maximized = false;
};

class Window {
public:
    Window();
    ~Window();

    bool Create(const WindowDesc& desc);
    void Destroy();

    // Pumps the Win32 message queue; returns false when a WM_QUIT was posted
    // (i.e. time to exit the application).
    bool PumpMessages();

    void SetTitle(const std::string& title);
    void Show();
    void SetFullscreen(bool fullscreen);
    void Resize(int width, int height);

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    bool IsMinimized() const { return m_Minimized; }

#if FW_PLATFORM_WINDOWS
    HWND Handle() const { return m_Hwnd; }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

    // Fired on WM_SIZE with the new client-area size.
    std::function<void(int, int)> OnResize;
    // Fired for every raw Win32 message before default handling, so ImGui's
    // Win32 backend can intercept input; return true to mark as "handled by ImGui".
    std::function<bool(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam)> OnRawMessage;

private:
    int m_Width = 0, m_Height = 0;
    bool m_Minimized = false;
    bool m_Fullscreen = false;
#if FW_PLATFORM_WINDOWS
    HWND m_Hwnd = nullptr;
    WINDOWPLACEMENT m_WindowedPlacement{};
#endif
};

} // namespace fw
