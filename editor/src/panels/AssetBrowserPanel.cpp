// Asset browser: a flat, filterable list view of everything under assets/
// (Godot's FileSystem dock, simplified). Double-clicking a scene opens it;
// double-clicking a script opens the script editor; dragging a mesh/prefab
// onto the viewport is left as a follow-up (selection + "Instantiate" button
// covers the same workflow for now).
#include "editor/EditorApp.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <fstream>
#include <filesystem>

namespace fw {

static const char* AssetTypeLabel(AssetType t) {
    switch (t) {
        case AssetType::Mesh: return "Mesh";
        case AssetType::Texture: return "Texture";
        case AssetType::Material: return "Material";
        case AssetType::Script: return "Script";
        case AssetType::Scene: return "Scene";
        case AssetType::Sound: return "Sound";
        case AssetType::Font: return "Font";
        case AssetType::Shader: return "Shader";
        default: return "File";
    }
}

void EditorApp::DrawAssetBrowserPanel() {
    ImGui::Begin("Assets");

    static std::string filter;
    static int typeFilter = 0;
    const char* typeNames[] = { "All", "Mesh", "Texture", "Material", "Script", "Scene", "Sound", "Font", "Shader" };

    if (ImGui::Button("Rescan")) m_AssetDatabase.Scan("assets");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::InputTextWithHint("##filter", "Search...", &filter);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##type", &typeFilter, typeNames, 9);

    ImGui::Separator();
    ImGui::BeginChild("AssetList");
    for (auto& entry : m_AssetDatabase.Entries()) {
        if (typeFilter != 0 && (int)entry.type != typeFilter) continue;
        if (!filter.empty() && entry.path.find(filter) == std::string::npos) continue;

        ImGui::PushID(entry.path.c_str());
        ImGui::TextDisabled("[%s]", AssetTypeLabel(entry.type));
        ImGui::SameLine();
        if (ImGui::Selectable(entry.path.c_str())) {
            if (entry.type == AssetType::Scene) OpenScene(entry.path);
            if (entry.type == AssetType::Script) {
                m_OpenScriptPath = entry.path;
                std::ifstream in(entry.path);
                m_ScriptEditBuffer.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ASSET_PATH", entry.path.c_str(), entry.path.size() + 1);
            ImGui::Text("%s", entry.path.c_str());
            ImGui::EndDragDropSource();
        }
        if (entry.type == AssetType::Mesh && ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Instantiate in Scene")) {
                Entity e = m_Engine.GetScene().CreateEntity(std::filesystem::path(entry.path).stem().string());
                e.AddOrReplace<MeshRendererComponent>().meshAsset = entry.path;
                m_SelectedEntity = e.Handle();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace fw
