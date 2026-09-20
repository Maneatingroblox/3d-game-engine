#include "engine/platform/Input.h"

namespace fw {

Input& Input::Get() {
    static Input instance;
    return instance;
}

void Input::OnKeyDown(int vk) {
    if (vk < 0 || vk >= 256) return;
    if (!m_Down[vk]) m_Pressed[vk] = true;
    m_Down[vk] = true;
}

void Input::OnKeyUp(int vk) {
    if (vk < 0 || vk >= 256) return;
    m_Down[vk] = false;
    m_Released[vk] = true;
}

void Input::OnMouseMove(int x, int y) {
    m_MousePos = vec2((float)x, (float)y);
}

void Input::OnMouseButton(int button, bool down) {
    if (button < 0 || button >= 8) return;
    m_MouseDown[button] = down;
}

void Input::OnMouseWheel(float delta) {
    m_WheelDelta += delta;
}

void Input::NewFrame() {
    m_Pressed.fill(false);
    m_Released.fill(false);
    m_MouseDelta = m_MousePos - m_LastMousePos;
    m_LastMousePos = m_MousePos;
    m_WheelDelta = 0.0f;
}

bool Input::IsKeyDown(int vk) const { return vk >= 0 && vk < 256 && m_Down[vk]; }
bool Input::WasKeyPressed(int vk) const { return vk >= 0 && vk < 256 && m_Pressed[vk]; }
bool Input::WasKeyReleased(int vk) const { return vk >= 0 && vk < 256 && m_Released[vk]; }
bool Input::IsMouseButtonDown(int button) const { return button >= 0 && button < 8 && m_MouseDown[button]; }

} // namespace fw
