// In-editor Lua script editor: a plain multi-line text box (no syntax
// highlighting - kept simple/dependency-free) with New / Open / Save / Reload,
// so a user can iterate on gameplay scripts without leaving the map maker.
// Saving while Play mode is running hot-reloads the script via ScriptEngine.
//
// Every file access goes through Paths::Resolve(): content paths are written
// relative to the project root ("assets/scripts/foo.lua"), which only resolves
// against the current working directory when the editor happens to be launched
// from the repo root. Without resolving, Open silently produced an empty buffer
// and Save wrote the file into whatever directory the process started in.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fw {

namespace {

const char* kScriptTemplate =
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

} // namespace

bool EditorApp::LoadScriptIntoEditor(const std::string& path) {
    if (path.empty()) return false;
    const std::string resolved = Paths::Resolve(path);
    std::ifstream in(resolved, std::ios::binary);
    if (!in) {
        FW_LOG_ERROR("Script Editor: could not open '%s'", resolved.c_str());
        m_ScriptStatus = "Could not open " + resolved;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    m_ScriptEditBuffer = ss.str();
    m_OpenScriptPath = path;
    m_ScriptDirty = false;
    m_ScriptStatus = "Opened " + resolved;
    FW_LOG_INFO("Script Editor: opened %s", resolved.c_str());
    return true;
}

bool EditorApp::SaveOpenScript() {
    if (m_OpenScriptPath.empty()) return false;
    const std::string resolved = Paths::Resolve(m_OpenScriptPath);

    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(resolved).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);

    std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
    if (!out) {
        FW_LOG_ERROR("Script Editor: could not write '%s'", resolved.c_str());
        m_ScriptStatus = "Could not write " + resolved;
        return false;
    }
    out << m_ScriptEditBuffer;
    out.close();

    m_ScriptDirty = false;
    m_ScriptStatus = "Saved " + resolved;
    FW_LOG_INFO("Script Editor: saved %s", resolved.c_str());

    // Hot-reload so an edit takes effect immediately while the game is running.
    if (m_Engine.State() != EngineRunState::Editing)
        m_Engine.GetScriptEngine().ReloadScript(m_OpenScriptPath);

    // Keep the asset browser in sync when a brand-new file was created.
    m_AssetDatabase.Scan(Paths::AssetRoot());
    return true;
}

void EditorApp::DrawScriptEditorPanel() {
    ImGui::Begin("Script Editor");

    // ---- toolbar (always available, even with no file open) ----------------
    // This whole row used to live *after* an early return taken when no script
    // was open, so with no file loaded the panel was an inert label: there was
    // no way to make a script from inside the editor at all.
    if (ImGui::Button("New")) {
        int n = 1;
        std::string candidate;
        do {
            candidate = "assets/scripts/new_script" + (n == 1 ? std::string() : std::to_string(n)) + ".lua";
            n++;
        } while (std::filesystem::exists(Paths::Resolve(candidate)) && n < 1000);

        m_OpenScriptPath = candidate;
        m_ScriptEditBuffer = kScriptTemplate;
        m_ScriptDirty = true;
        m_ScriptStatus = "New script (unsaved): " + candidate;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create a new Lua script from the standard callback template");

    ImGui::SameLine();
    if (ImGui::Button("Open...")) ImGui::OpenPopup("OpenScriptPopup");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick any .lua file found under assets/");

    ImGui::SameLine();
    ImGui::BeginDisabled(m_OpenScriptPath.empty());
    if (ImGui::Button("Save")) SaveOpenScript();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(m_OpenScriptPath.empty());
    if (ImGui::Button("Reload")) LoadScriptIntoEditor(m_OpenScriptPath);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Discard changes and re-read the file from disk");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(m_OpenScriptPath.empty() || m_SelectedEntity == entt::null);
    if (ImGui::Button("Attach to Selection")) {
        Entity sel(m_SelectedEntity, &m_Engine.GetScene().Registry());
        if (sel.IsValid()) {
            sel.AddOrReplace<ScriptComponent>().scriptAsset = m_OpenScriptPath;
            m_ScriptStatus = "Attached to '" + sel.Name() + "'";
            FW_LOG_INFO("Attached script '%s' to '%s'", m_OpenScriptPath.c_str(), sel.Name().c_str());
        }
    }
    ImGui::EndDisabled();

    // ---- "Open..." popup: every .lua the asset database knows about --------
    if (ImGui::BeginPopup("OpenScriptPopup")) {
        ImGui::TextDisabled("Lua scripts under assets/");
        ImGui::Separator();
        bool any = false;
        for (const auto& entry : m_AssetDatabase.Entries()) {
            if (entry.type != AssetType::Script) continue;
            any = true;
            if (ImGui::MenuItem(entry.path.c_str())) LoadScriptIntoEditor(entry.path);
        }
        if (!any) ImGui::TextDisabled("(none found - press New to create one)");
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (m_OpenScriptPath.empty()) {
        ImGui::TextDisabled("No script open.");
        ImGui::Spacing();
        ImGui::TextWrapped("Press New to start a script, Open... to edit an existing one, or "
                           "double-click a .lua file in the Asset Browser.");
        if (!m_ScriptStatus.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_ScriptStatus.c_str());
        }
        ImGui::End();
        return;
    }

    ImGui::Text("%s%s", m_OpenScriptPath.c_str(), m_ScriptDirty ? " *" : "");
    if (!m_ScriptStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("- %s", m_ScriptStatus.c_str());
    }

    // Ctrl+S saves while the editor has focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveOpenScript();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (ImGui::InputTextMultiline("##scriptsrc", &m_ScriptEditBuffer, avail,
                                  ImGuiInputTextFlags_AllowTabInput)) {
        m_ScriptDirty = true;
    }

    ImGui::End();
}

} // namespace fw
