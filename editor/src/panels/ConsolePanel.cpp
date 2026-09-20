// Console panel: displays fw::Log's ring buffer plus any Lua script errors
// collected by ScriptEngine, colour-coded by severity.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include <imgui.h>

namespace fw {

void EditorApp::DrawConsolePanel() {
    ImGui::Begin("Console");
    if (ImGui::Button("Clear")) Log::Get().Clear();
    ImGui::SameLine();
    if (ImGui::Button("Clear Script Errors")) m_Engine.GetScriptEngine().ClearErrors();

    ImGui::Separator();
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& err : m_Engine.GetScriptEngine().Errors()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[LUA ERROR] %s", err.c_str());
    }

    for (auto& entry : Log::Get().Snapshot()) {
        ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
        switch (entry.level) {
            case LogLevel::Trace: color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
            case LogLevel::Warn:  color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
            case LogLevel::Error: color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f); break;
            default: break;
        }
        ImGui::TextColored(color, "%s", entry.message.c_str());
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

} // namespace fw
