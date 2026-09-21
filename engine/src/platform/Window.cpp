#include "engine/platform/Window.h"
#include "engine/core/Log.h"

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
        // Paint the client area with the window class background brush instead
        // of letting Windows fill it with the default (white) colour: a window
        // whose first frame hasn't been presented yet should look dark like the
        // engine's clear colour, not like a broken blank window.
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE: {
            if (self) {
                self->m_Width = LOWORD(lParam);
                self->m_Height = HIWORD(lParam);
                self->m_Minimized = (wParam == SIZE_MINIMIZED);
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
    if (m_Hwnd) {
        DestroyWindow(m_Hwnd);
        m_Hwnd = nullptr;
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
