// In-editor Lua script editor: a plain multi-line text box (no syntax
// highlighting - kept simple/dependency-free) with New / Open / Save / Reload.
// Paths are project-relative and resolved through Paths::Resolve, so scripts
// open, save and hot-reload correctly no matter what directory the editor was
// launched from. Saving while Play mode is running hot-reloads the script via
// ScriptEngine.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iterator>

namespace fw {

void EditorApp::OpenScript(const std::string& path) {
    const std::string resolved = Paths::Resolve(path);
    std::ifstream in(resolved, std::ios::binary);
    if (!in) {
        m_OpenScriptPath.clear();
        m_ScriptEditBuffer.clear();
        m_ScriptDirty = false;
        m_ScriptStatus = "Could not open " + resolved;
        FW_LOG_ERROR("Script editor: could not open '%s'", resolved.c_str());
        return;
    }
    m_OpenScriptPath = path;
    m_ScriptEditBuffer.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    m_ScriptDirty = false;
    m_ScriptStatus = "Opened " + path;
    FW_LOG_INFO("Script editor: opened '%s'", resolved.c_str());
    // Bring the tab to the front so clicking a script actually shows the editor.
    ImGui::SetWindowFocus("Script Editor");
}

bool EditorApp::SaveOpenScript() {
    if (m_OpenScriptPath.empty()) return false;
    const std::string resolved = Paths::Resolve(m_OpenScriptPath);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(resolved).parent_path(), ec);
    std::ofstream out(resolved, std::ios::trunc | std::ios::binary);
    if (!out) {
        m_ScriptStatus = "Save FAILED: " + resolved;
        FW_LOG_ERROR("Script editor: could not write '%s'", resolved.c_str());
        return false;
    }
    out << m_ScriptEditBuffer;
    out.close();
    m_ScriptDirty = false;
    m_ScriptStatus = "Saved " + m_OpenScriptPath;
    FW_LOG_INFO("Saved script: %s", resolved.c_str());
    if (m_Engine.State() != EngineRunState::Editing) {
        // Hot reload while playing/paused so the change is visible immediately.
        m_Engine.GetScriptEngine().ReloadScript(m_OpenScriptPath);
        m_ScriptStatus += " (hot-reloaded)";
    }
    return true;
}

void EditorApp::NewScriptFile() {
    namespace fs = std::filesystem;
    const std::string dir = "assets/scripts";
    std::string path;
    for (int i = 1; i < 1000; i++) {
        path = (i == 1) ? (dir + "/new_script.lua")
                        : (dir + "/new_script_" + std::to_string(i) + ".lua");
        if (!fs::exists(Paths::Resolve(path))) break;
    }
    m_OpenScriptPath = path;
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
    m_ScriptDirty = true;
    if (SaveOpenScript()) {
        m_ScriptStatus = "Created " + m_OpenScriptPath;
        m_AssetDatabase.Scan("assets"); // make the new file show up in the browser
    }
}

void EditorApp::DrawScriptEditorPanel() {
    // Fixed title (and therefore a stable dock + focus target); the open file
    // and modified state are shown in the header row below.
    ImGui::Begin("Script Editor");

    // ---- header: open file + modified marker ------------------------------
    if (m_OpenScriptPath.empty()) {
        ImGui::TextDisabled("No script open");
    } else {
        ImGui::Text("File: %s%s", m_OpenScriptPath.c_str(), m_ScriptDirty ? "  *" : "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Paths::Resolve(m_OpenScriptPath).c_str());
    }

    // ---- toolbar: always available, even with no script open (this used to
    // hide "New Script" behind an open script - there was no way in).
    if (ImGui::BeginMenu("Open...")) {
        bool any = false;
        for (auto& entry : m_AssetDatabase.Entries()) {
            if (entry.type != AssetType::Script) continue;
            any = true;
            if (ImGui::MenuItem(entry.path.c_str())) OpenScript(entry.path);
        }
        if (!any) ImGui::TextDisabled("No .lua files under assets/");
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) NewScriptFile();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_OpenScriptPath.empty());
    if (ImGui::Button("Save")) SaveOpenScript();
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        if (!m_OpenScriptPath.empty()) OpenScript(m_OpenScriptPath);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+S saves");

    if (!m_ScriptStatus.empty()) {
        ImGui::SameLine();
        const bool err = m_ScriptStatus.rfind("Could not", 0) == 0 ||
                         m_ScriptStatus.rfind("Save FAILED", 0) == 0;
        ImGui::TextColored(err ? ImVec4(1, 0.4f, 0.35f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1),
                           "%s", m_ScriptStatus.c_str());
    }

    ImGui::Separator();

    if (m_OpenScriptPath.empty()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("ScriptEmpty", avail, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        const char* hint = "No script open.\n\nUse Open... above, double-click a .lua in the Assets\n"
                           "panel, or press New to create one.";
        ImVec2 textSize = ImGui::CalcTextSize(hint);
        ImVec2 winSize = ImGui::GetWindowSize();
        ImGui::SetCursorPos(ImVec2((winSize.x - textSize.x) * 0.5f, (winSize.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("%s", hint);
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // Ctrl+S saves even while typing in the box.
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        SaveOpenScript();
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing() + 4.0f; // status line under the editor
    if (avail.y < 64.0f) avail.y = 64.0f;
    if (ImGui::InputTextMultiline("##scriptsrc", &m_ScriptEditBuffer, avail,
                                  ImGuiInputTextFlags_AllowTabInput)) {
        m_ScriptDirty = true;
    }
    ImGui::TextDisabled("%d lines | %s%s", 1 + (int)std::count(m_ScriptEditBuffer.begin(), m_ScriptEditBuffer.end(), '\n'),
                        Paths::Resolve(m_OpenScriptPath).c_str(), m_ScriptDirty ? " (modified)" : "");

    ImGui::End();
}

} // namespace fw
