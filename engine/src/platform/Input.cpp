#include "engine/platform/Input.h"
#include "engine/script/ScriptEngine.h"

namespace fw {

// Mouse buttons also masquerade as the Win32 VK_LBUTTON/VK_RBUTTON/... key
// slots so both styles of query work:
//   IsMouseButtonDown(0) / (VK_LBUTTON & 7)-style indices for mouse code,
//   WasKeyPressed(VK_LBUTTON) for key-style edge queries.
static int ButtonToVk(int button) {
    switch (button) {
        case 0: return VK_LBUTTON;
        case 1: return VK_RBUTTON;
        case 2: return VK_MBUTTON;
        default: return -1;
    }
}

Input& Input::Get() {
    static Input instance;
    return instance;
}

void Input::OnKeyDown(int vk) {
    if (vk < 0 || vk >= 256) return;
    if (!m_Down[vk]) m_Pressed[vk] = true;
    m_Down[vk] = true;
    if (m_ScriptEngine) m_ScriptEngine->DispatchKeyDown(vk);
}

void Input::OnKeyUp(int vk) {
    if (vk < 0 || vk >= 256) return;
    m_Down[vk] = false;
    m_Released[vk] = true;
    if (m_ScriptEngine) m_ScriptEngine->DispatchKeyUp(vk);
}

void Input::OnMouseMove(int x, int y) {
    const vec2 pos((float)x, (float)y);
    const vec2 delta = pos - m_MousePos;
    m_MouseDelta += delta;
    m_MousePos = pos;
    if (m_ScriptEngine) m_ScriptEngine->DispatchMouseMove(m_MousePos, delta);
}

void Input::OnMouseButton(int button, bool down) {
    if (button < 0 || button >= 8) return;
    m_MouseDown[button] = down;
    const int vk = ButtonToVk(button);
    if (vk >= 0 && vk < 256) {
        if (down && !m_Down[vk]) m_Pressed[vk] = true;
        if (!down && m_Down[vk]) m_Released[vk] = true;
        m_Down[vk] = down;
    }
    if (m_ScriptEngine) m_ScriptEngine->DispatchMouseButton(button, down);
}

void Input::OnMouseWheel(float delta) {
    m_WheelDelta += delta;
    if (m_ScriptEngine) m_ScriptEngine->DispatchMouseWheel(delta);
}

void Input::NewFrame() {
    m_Pressed.fill(false);
    m_Released.fill(false);
    m_MouseDelta = vec2(0.0f);
    m_WheelDelta = 0.0f;
    if (m_ScriptEngine) m_ScriptEngine->BeginInputFrame();
}

void Input::SetMousePositionSilent(const vec2& pos) {
    m_MousePos = pos;
}

bool Input::IsKeyDown(int vk) const { return vk >= 0 && vk < 256 && m_Down[vk]; }
bool Input::WasKeyPressed(int vk) const { return vk >= 0 && vk < 256 && m_Pressed[vk]; }
bool Input::WasKeyReleased(int vk) const { return vk >= 0 && vk < 256 && m_Released[vk]; }
bool Input::IsMouseButtonDown(int button) const { return button >= 0 && button < 8 && m_MouseDown[button]; }

} // namespace fw
