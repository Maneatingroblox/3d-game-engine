#pragma once
// Central input state, fed by Win32 raw messages (WM_KEYDOWN/UP, mouse) and
// queried by both the editor (camera fly-through, gizmo interaction) and, at
// runtime, by the ScriptEngine's `Input` Lua table.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <array>

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
    ScriptEngine* m_ScriptEngine = nullptr;
};

} // namespace fw
