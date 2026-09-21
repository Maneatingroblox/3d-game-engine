#pragma once
// Win32 window wrapper. Owns the HWND, pumps the message loop, and forwards
// input/resize events to whoever registered callbacks (the editor's ImGui
// backend, and engine/platform/Input).
//
// On Windows the window can be presented through GDI (BlitSoftwareFrame) as
// well as through the D3D11 swap chain: the software path is what guarantees
// the editor shows *something* even when the GPU path is unavailable.
// On other hosts (headless validation, tools/ui_shot) the class is inert.

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

#if FW_PLATFORM_WINDOWS

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

    HWND Handle() const { return m_Hwnd; }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ---- software presentation (GDI) ---------------------------------------
    // Copies a top-down RGBA8 frame into a DIB section and blits it to the
    // client area with BitBlt. This is the whole "GPU-less" display path: it
    // needs no D3D11 device, no swap chain and no shaders, so the editor can
    // always put pixels on screen. The last frame is remembered so WM_PAINT can
    // repaint after the window was covered or resized.
    void BlitSoftwareFrame(const u8* rgba, int width, int height);
    bool HasSoftwareFrame() const { return m_SoftBitmap != nullptr; }

    // Fired on WM_SIZE with the new client-area size.
    std::function<void(int, int)> OnResize;
    // Fired for every raw Win32 message before default handling, so ImGui's
    // Win32 backend can intercept input; return true to mark as "handled by ImGui".
    std::function<bool(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam)> OnRawMessage;

private:
    void BlitSoftwareFrameTo(HDC target, int width, int height);

    int m_Width = 0, m_Height = 0;
    bool m_Minimized = false;
    bool m_Fullscreen = false;
    HWND m_Hwnd = nullptr;
    WINDOWPLACEMENT m_WindowedPlacement{};

    // Software (GDI) presentation surface.
    HDC m_SoftDC = nullptr;
    HBITMAP m_SoftBitmap = nullptr;
    HGDIOBJ m_SoftOldBitmap = nullptr;
    void* m_SoftBits = nullptr;
    int m_SoftWidth = 0, m_SoftHeight = 0;
};

#else // !FW_PLATFORM_WINDOWS

// Host/headless builds: there is no Win32 layer, so the window is a stub. Code
// that owns a Window (Application, EditorApp) still compiles and links, which
// is what lets the editor UI be rendered and screenshotted without a display
// (see tools/ui_shot.cpp).
class Window {
public:
    Window() = default;
    ~Window() = default;

    bool Create(const WindowDesc& desc) {
        m_Width = desc.width;
        m_Height = desc.height;
        return false;  // no window on this host; callers fall back to headless
    }
    void Destroy() {}
    bool PumpMessages() { return true; }
    void SetTitle(const std::string&) {}
    void Show() {}
    void SetFullscreen(bool) {}
    void Resize(int width, int height) { m_Width = width; m_Height = height; }
    void BlitSoftwareFrame(const u8*, int, int) {}

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    bool IsMinimized() const { return false; }

    std::function<void(int, int)> OnResize;
    std::function<bool(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam)> OnRawMessage;

private:
    int m_Width = 0, m_Height = 0;
};

#endif // FW_PLATFORM_WINDOWS

} // namespace fw
