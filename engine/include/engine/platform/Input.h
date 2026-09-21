#pragma once
// Central input state, fed by Win32 raw messages (WM_KEYDOWN/UP, mouse) and
// queried by both the editor (camera fly-through, gizmo interaction) and, at
// runtime, by the ScriptEngine's `Input` Lua table.

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
        #define VK_F9       0x79
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
    void OnMouseButton(int button, bool down);
    void OnMouseWheel(float delta);

    // Call once per frame after processing all messages, to reset
    // "just pressed/released" edge state and compute mouse delta.
    void NewFrame();

    // Clears every held key/button. Called when the window loses focus so keys
    // held at that moment don't stay "down" forever (which used to send the
    // editor camera drifting after alt-tab).
    void ResetState();

    // True while the UI layer (Dear ImGui) owns the keyboard/mouse - e.g. a
    // text box has focus, or the cursor is over a panel rather than the 3D
    // viewport. Set once per frame by the application; consumers such as the
    // editor fly camera use it to ignore input meant for the interface.
    void SetUICapture(bool keyboard, bool mouse) { m_UIWantsKeyboard = keyboard; m_UIWantsMouse = mouse; }
    bool UIWantsKeyboard() const { return m_UIWantsKeyboard; }
    bool UIWantsMouse() const { return m_UIWantsMouse; }

    bool IsKeyDown(int vkCode) const;
    bool WasKeyPressed(int vkCode) const;
    bool WasKeyReleased(int vkCode) const;
    bool IsMouseButtonDown(int button) const;
    vec2 MousePosition() const { return m_MousePos; }
    vec2 MouseDelta() const { return m_MouseDelta; }
    float WheelDelta() const { return m_WheelDelta; }

    void SetCursorLocked(bool locked) { m_CursorLocked = locked; }
    bool CursorLocked() const { return m_CursorLocked; }

    // Optional link so this class can push updates straight into the
    // scripting layer's global input tables when running without editor UI.
    void BindScriptEngine(ScriptEngine* se) { m_ScriptEngine = se; }

private:
    std::array<bool, 256> m_Down{};
    std::array<bool, 256> m_Pressed{};
    std::array<bool, 256> m_Released{};
    bool m_MouseDown[8] = { false };
    vec2 m_MousePos{0.0f};
    vec2 m_LastMousePos{0.0f};
    vec2 m_MouseDelta{0.0f};
    float m_WheelDelta = 0.0f;
    bool m_CursorLocked = false;
    bool m_UIWantsKeyboard = false;
    bool m_UIWantsMouse = false;
    ScriptEngine* m_ScriptEngine = nullptr;
};

} // namespace fw
