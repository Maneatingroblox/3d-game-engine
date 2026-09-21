#pragma once
// Central input state, fed by Win32 raw messages (WM_KEYDOWN/UP, mouse) and
// queried by both the editor (camera fly-through, gizmo interaction) and, at
// runtime, by the ScriptEngine's `Input` Lua table.
//
// Frame protocol (see Application::MainLoop):
//   1. Input::Get().NewFrame()      - clears last frame's edge state
//   2. Window::PumpMessages()       - WndProc feeds OnKeyDown/OnMouse*/...
//   3. OnUpdate()/OnImGui()         - consumers read the per-frame state
// Mouse motion accumulates into a per-frame delta while it is pumped, so the
// fly camera gets exactly the movement that happened this frame.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <array>

// The engine's key convention is the Win32 virtual-key code: the Win32 window
// feeds WM_KEYDOWN straight into Input::OnKeyDown() and scripts/editors query it
// with those codes. Hosts without windows.h (headless validation, tools/fwui)
// get the same numeric values here so platform-independent code - the editor,
// its panels, the game - compiles and runs unchanged.
#if !FW_PLATFORM_WINDOWS
    #ifndef VK_LBUTTON
        #define VK_LBUTTON  0x01
        #define VK_RBUTTON  0x02
        #define VK_MBUTTON  0x04
        #define VK_BACK     0x08
        #define VK_TAB      0x09
        #define VK_RETURN   0x0D
        #define VK_SHIFT    0x10
        #define VK_CONTROL  0x11
        #define VK_MENU     0x12
        #define VK_ESCAPE   0x1B
        #define VK_SPACE    0x20
        #define VK_LEFT     0x25
        #define VK_UP       0x26
        #define VK_RIGHT    0x27
        #define VK_DOWN     0x28
        #define VK_DELETE   0x2E
        #define VK_F1       0x70
        #define VK_F2       0x71
        #define VK_F3       0x72
        #define VK_F4       0x73
        #define VK_F5       0x74
        #define VK_F6       0x75
        #define VK_F7       0x76
        #define VK_F8       0x77
        #define VK_F9       0x78
        #define VK_F10      0x79
        #define VK_F11      0x7A
        #define VK_F12      0x7B
    #endif
#endif

namespace fw {

class ScriptEngine;

class Input {
public:
    static Input& Get();

    void OnKeyDown(int vkCode);
    void OnKeyUp(int vkCode);
    void OnMouseMove(int x, int y);
    // `button` is the mouse button index: 0 = left, 1 = right, 2 = middle,
    // 3/4 = X buttons. The state is also mirrored into the VK_LBUTTON/
    // VK_RBUTTON/VK_MBUTTON key slots so WasKeyPressed(VK_LBUTTON) works.
    void OnMouseButton(int button, bool down);
    // Wheel notches (already divided by WHEEL_DELTA by the window layer).
    void OnMouseWheel(float delta);

    // Call once per frame BEFORE pumping window messages: resets the
    // per-frame edge state (pressed/released), wheel and mouse-delta
    // accumulators. Movement and clicks that arrive during the pump become
    // this frame's input.
    void NewFrame();

    bool IsKeyDown(int vkCode) const;
    bool WasKeyPressed(int vkCode) const;
    bool WasKeyReleased(int vkCode) const;
    // button: 0 = left, 1 = right, 2 = middle (see OnMouseButton).
    bool IsMouseButtonDown(int button) const;
    vec2 MousePosition() const { return m_MousePos; }
    // Mouse movement accumulated during this frame (pixels).
    vec2 MouseDelta() const { return m_MouseDelta; }
    float WheelDelta() const { return m_WheelDelta; }

    // Warps the OS cursor without producing a movement delta - used when the
    // cursor is locked/recentred so the re-centre itself doesn't cancel the
    // real mouse motion of the frame.
    void SetMousePositionSilent(const vec2& pos);

    // Cursor lock (used by the game's mouselook and the editor's fly mode):
    // the platform layer clips/hides the cursor and recentres it every frame.
    void SetCursorLocked(bool locked) { m_CursorLocked = locked; }
    bool CursorLocked() const { return m_CursorLocked; }

    // Optional link so this class can push updates straight into the
    // scripting layer's global input tables (Input.* in Lua).
    void BindScriptEngine(ScriptEngine* se) { m_ScriptEngine = se; }

private:
    std::array<bool, 256> m_Down{};
    std::array<bool, 256> m_Pressed{};
    std::array<bool, 256> m_Released{};
    bool m_MouseDown[8] = { false };
    vec2 m_MousePos{0.0f};
    vec2 m_MouseDelta{0.0f};
    float m_WheelDelta = 0.0f;
    bool m_CursorLocked = false;
    ScriptEngine* m_ScriptEngine = nullptr;
};

} // namespace fw
