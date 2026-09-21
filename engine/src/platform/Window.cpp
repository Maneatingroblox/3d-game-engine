#include "engine/platform/Window.h"
#include "engine/platform/Input.h"
#include "engine/core/Log.h"
#include <algorithm>
#include <cstring>

#if FW_PLATFORM_WINDOWS

namespace fw {

static const wchar_t* kClassName = L"ForgeworksWindowClass";

Window::Window() = default;
Window::~Window() { Destroy(); }

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self && self->OnRawMessage) {
        if (self->OnRawMessage(hwnd, msg, (unsigned long long)wParam, (long long)lParam)) {
            // ImGui or another handler consumed it; still let WM_SIZE/CLOSE/DESTROY
            // fall through so the app stays correctly sized/closable.
        }
    }

    switch (msg) {
        // ---- feed engine/platform/Input -------------------------------------
        // Always, even when ImGui's Win32 backend also saw the message: the
        // engine's input state must not depend on UI focus. Button indices:
        // 0 = left, 1 = right, 2 = middle, 3/4 = X buttons. Mirroring mouse
        // buttons into the VK_LBUTTON.. slots keeps WasKeyPressed(VK_LBUTTON)
        // working alongside IsMouseButtonDown.
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            Input::Get().OnKeyDown((int)wParam);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            Input::Get().OnKeyUp((int)wParam);
            break;
        case WM_MOUSEMOVE: {
            // Signed (short) coords: the cursor can legitimately be outside
            // the client area (negative or beyond size) while dragging.
            const int mx = (int)(short)LOWORD(lParam);
            const int my = (int)(short)HIWORD(lParam);
            Input::Get().OnMouseMove(mx, my);
            break;
        }
        case WM_LBUTTONDOWN: Input::Get().OnMouseButton(0, true);  break;
        case WM_LBUTTONUP:   Input::Get().OnMouseButton(0, false); break;
        case WM_RBUTTONDOWN: Input::Get().OnMouseButton(1, true);  break;
        case WM_RBUTTONUP:   Input::Get().OnMouseButton(1, false); break;
        case WM_MBUTTONDOWN: Input::Get().OnMouseButton(2, true);  break;
        case WM_MBUTTONUP:   Input::Get().OnMouseButton(2, false); break;
        case WM_XBUTTONDOWN:
            Input::Get().OnMouseButton(HIWORD(wParam) == XBUTTON1 ? 3 : 4, true);
            break;
        case WM_XBUTTONUP:
            Input::Get().OnMouseButton(HIWORD(wParam) == XBUTTON1 ? 3 : 4, false);
            break;
        case WM_MOUSEWHEEL:
            // Win32 reports 120 per notch; Input wants wheel notches.
            Input::Get().OnMouseWheel((float)(short)HIWORD(wParam) / 120.0f);
            break;

        // Paint the client area with the window class background brush instead
        // of letting Windows fill it with the default (white) colour: a window
        // whose first frame hasn't been presented yet should look dark like the
        // engine's clear colour, not like a broken blank window.
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            // Repaint from the last software frame (if any) instead of letting
            // Windows fill the client area: a window that is covered, moved or
            // resized must keep showing the editor rather than a blank rect.
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            if (self) self->BlitSoftwareFrameTo(dc, ps.rcPaint.right, ps.rcPaint.bottom);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE: {
            if (self) {
                self->m_Width = LOWORD(lParam);
                self->m_Height = HIWORD(lParam);
                self->m_Minimized = (wParam == SIZE_MINIMIZED);
                self->m_Maximized = (wParam == SIZE_MAXIMIZED) || IsZoomed(hwnd);
                if (self->OnResize && wParam != SIZE_MINIMIZED) self->OnResize(self->m_Width, self->m_Height);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Window::Create(const WindowDesc& desc) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = &Window::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 22)); // engine clear colour
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        // Class already registered by a previous instance - fine; otherwise log.
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
            FW_LOG_WARN("RegisterClassExW failed (error %lu)", err);
    }

    DWORD style = desc.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);

    RECT rect{ 0, 0, desc.width, desc.height };
    AdjustWindowRect(&rect, style, FALSE);

    std::wstring wideTitle(desc.title.begin(), desc.title.end());

    m_Hwnd = CreateWindowExW(
        0, kClassName, wideTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!m_Hwnd) {
        FW_LOG_ERROR("Failed to create Win32 window");
        return false;
    }

    SetWindowLongPtrW(m_Hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_Width = desc.width;
    m_Height = desc.height;

    if (desc.maximized) ShowWindow(m_Hwnd, SW_MAXIMIZE);
    else ShowWindow(m_Hwnd, SW_SHOW);
    UpdateWindow(m_Hwnd);
    m_Maximized = IsZoomed(m_Hwnd) != 0;

    // Launched from a terminal / shortcut the new window can end up behind it;
    // bring it to the front so "nothing appeared" can't be a focus problem.
    SetForegroundWindow(m_Hwnd);
    SetFocus(m_Hwnd);

    RECT client{};
    GetClientRect(m_Hwnd, &client);
    FW_LOG_INFO("Window created: '%s' requested %dx%d, client area %ldx%ld (visible=%d)",
                desc.title.c_str(), desc.width, desc.height,
                client.right - client.left, client.bottom - client.top, IsWindowVisible(m_Hwnd) ? 1 : 0);
    return true;
}

void Window::Destroy() {
    if (m_SoftBitmap) {
        if (m_SoftDC && m_SoftOldBitmap) SelectObject(m_SoftDC, m_SoftOldBitmap);
        DeleteObject(m_SoftBitmap);
        m_SoftBitmap = nullptr;
        m_SoftBits = nullptr;
        m_SoftWidth = m_SoftHeight = 0;
    }
    if (m_SoftDC) {
        DeleteDC(m_SoftDC);
        m_SoftDC = nullptr;
    }
    if (m_Hwnd) {
        DestroyWindow(m_Hwnd);
        m_Hwnd = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Software (GDI) presentation
// ---------------------------------------------------------------------------
// The GPU-less display path: a top-down 32-bit DIB section holds the CPU
// framebuffer (engine/render/SoftCanvas.h) and BitBlt pushes it to the client
// area. No D3D11 device, swap chain or shader is involved, so the editor can
// always show its interface.
void Window::BlitSoftwareFrameTo(HDC target, int width, int height) {
    if (!target || !m_SoftDC || !m_SoftBitmap || m_SoftWidth <= 0 || m_SoftHeight <= 0) return;
    const int w = std::min(width, m_SoftWidth);
    const int h = std::min(height, m_SoftHeight);
    if (w > 0 && h > 0) BitBlt(target, 0, 0, w, h, m_SoftDC, 0, 0, SRCCOPY);
}

void Window::BlitSoftwareFrame(const u8* rgba, int width, int height) {
    if (!m_Hwnd || !rgba || width <= 0 || height <= 0) return;

    if (!m_SoftBitmap || width != m_SoftWidth || height != m_SoftHeight) {
        if (!m_SoftDC) m_SoftDC = CreateCompatibleDC(nullptr);
        if (!m_SoftDC) { FW_LOG_ERROR("Software present: CreateCompatibleDC failed"); return; }
        if (m_SoftBitmap) {
            if (m_SoftOldBitmap) SelectObject(m_SoftDC, m_SoftOldBitmap);
            DeleteObject(m_SoftBitmap);
            m_SoftBitmap = nullptr;
            m_SoftBits = nullptr;
        }

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height;  // negative: top-down, like SoftCanvas
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        m_SoftBitmap = CreateDIBSection(m_SoftDC, &bi, DIB_RGB_COLORS, &m_SoftBits, nullptr, 0);
        if (!m_SoftBitmap || !m_SoftBits) {
            FW_LOG_ERROR("Software present: CreateDIBSection failed (%dx%d)", width, height);
            m_SoftBitmap = nullptr;
            m_SoftBits = nullptr;
            return;
        }
        m_SoftOldBitmap = SelectObject(m_SoftDC, m_SoftBitmap);
        m_SoftWidth = width;
        m_SoftHeight = height;
    }

    // A 32-bit DIB section is 4-byte aligned, and so is width*4, so the rows
    // can be copied in one go.
    std::memcpy(m_SoftBits, rgba, (size_t)width * (size_t)height * 4u);

    HDC dc = GetDC(m_Hwnd);
    if (dc) {
        BlitSoftwareFrameTo(dc, width, height);
        ReleaseDC(m_Hwnd, dc);
    }
}

bool Window::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void Window::SetCursorLocked(bool locked) {
    if (m_CursorLocked == locked) return;
    m_CursorLocked = locked;
    if (locked) {
        // Confine the cursor to the client area and hide it while looking.
        RECT rc;
        GetClientRect(m_Hwnd, &rc);
        POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
        ClientToScreen(m_Hwnd, &tl);
        ClientToScreen(m_Hwnd, &br);
        const RECT screen{tl.x, tl.y, br.x, br.y};
        ClipCursor(&screen);
        ShowCursor(FALSE);
        CentreCursor();
    } else {
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    }
}

void Window::CentreCursor() {
    if (!m_Hwnd) return;
    RECT rc;
    GetClientRect(m_Hwnd, &rc);
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    POINT c{cx, cy};
    ClientToScreen(m_Hwnd, &c);
    SetCursorPos(c.x, c.y);
    // Keep Input's tracked position in sync: the synthetic WM_MOUSEMOVE this
    // generates must produce a zero delta, not a snap back to the centre.
    Input::Get().SetMousePositionSilent(vec2((float)cx, (float)cy));
}

void Window::SetTitle(const std::string& title) {
    std::wstring wide(title.begin(), title.end());
    SetWindowTextW(m_Hwnd, wide.c_str());
}

void Window::Show() { ShowWindow(m_Hwnd, SW_SHOW); }

void Window::SetFullscreen(bool fullscreen) {
    if (fullscreen == m_Fullscreen) return;
    m_Fullscreen = fullscreen;
    if (fullscreen) {
        GetWindowPlacement(m_Hwnd, &m_WindowedPlacement);
        SetWindowLongW(m_Hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        HMONITOR monitor = MonitorFromWindow(m_Hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(monitor, &mi);
        SetWindowPos(m_Hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(m_Hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(m_Hwnd, &m_WindowedPlacement);
        SetWindowPos(m_Hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

void Window::Resize(int width, int height) {
    RECT rect{ 0, 0, width, height };
    AdjustWindowRect(&rect, (DWORD)GetWindowLongW(m_Hwnd, GWL_STYLE), FALSE);
    SetWindowPos(m_Hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER);
}

} // namespace fw

#endif // FW_PLATFORM_WINDOWS
