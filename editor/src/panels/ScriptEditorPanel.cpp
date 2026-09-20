// Minimal in-editor Lua script editor: a plain multi-line text box (no
// syntax highlighting - kept simple/dependency-free) with Save + Reload,
// so a user can iterate on gameplay scripts without leaving the map maker.
// Saving while Play mode is running hot-reloads the script via ScriptEngine.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <fstream>

namespace fw {

void EditorApp::DrawScriptEditorPanel() {
    ImGui::Begin("Script Editor");

    if (m_OpenScriptPath.empty()) {
        ImGui::TextDisabled("Open a .lua script from the Asset Browser or an entity's Script component.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", m_OpenScriptPath.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Save")) {
        std::ofstream out(m_OpenScriptPath, std::ios::trunc);
        out << m_ScriptEditBuffer;
        out.close();
        FW_LOG_INFO("Saved script: %s", m_OpenScriptPath.c_str());
        if (m_Engine.State() != EngineRunState::Editing) {
            m_Engine.GetScriptEngine().ReloadScript(m_OpenScriptPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New Script")) {
        m_OpenScriptPath = "assets/scripts/new_script.lua";
        m_ScriptEditBuffer =
            "-- Called once when the entity starts (Play mode entered / entity spawned)\n"
            "function OnStart(self)\n"
            "end\n\n"
            "-- Called every frame with delta time in seconds\n"
            "function OnUpdate(self, dt)\n"
            "end\n\n"
            "-- Called on a fixed physics timestep\n"
            "function OnFixedUpdate(self, dt)\n"
            "end\n\n"
            "function OnDestroy(self)\n"
            "end\n";
    }

    ImGui::Separator();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##scriptsrc", &m_ScriptEditBuffer, avail,
        ImGuiInputTextFlags_AllowTabInput);

    ImGui::End();
}

} // namespace fw
