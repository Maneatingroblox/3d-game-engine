// "Bake Lighting" panel, matching Godot's LightmapGI bake button / Unity's
// Lighting window "Generate Lighting" workflow. The bake runs synchronously
// on the main thread: Lightmapper::BakeScene mutates ECS components
// (MeshRendererComponent::lightmapAsset/useLightmap) directly, and nothing
// else here is set up to make the EnTT registry safe to touch concurrently,
// so we deliberately don't background this onto a worker thread.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/lightmap/Lightmapper.h"
#include <imgui.h>

namespace fw {

void EditorApp::DrawLightmapBakePanel() {
    ImGui::Begin("Lighting");

    static LightmapBakeSettings settings;
    ImGui::SliderInt("Resolution", &settings.resolution, 64, 2048);
    ImGui::SliderInt("Samples/Texel", &settings.samplesPerTexel, 4, 256);
    ImGui::SliderInt("Bounces", &settings.bounces, 0, 3);
    ImGui::SliderFloat("Ambient Boost", &settings.ambientBoost, 0.0f, 1.0f);
    ImGui::ColorEdit3("Sky Color", &settings.skyColor.x);

    ImGui::Separator();

    ImGui::BeginDisabled(m_Engine.State() != EngineRunState::Editing);
    if (ImGui::Button("Bake Lighting", ImVec2(-1, 32))) {
        m_BakeProgress = 0.0f;
        m_BakeStatus = "Baking...";
        Lightmapper::BakeScene(m_Engine.GetScene(), settings, [this](float pct, const std::string& what) {
            m_BakeProgress = pct;
            m_BakeStatus = what;
        });
        m_BakeProgress = -1.0f;
        m_BakeStatus = "Bake complete.";
        FW_LOG_INFO("Lightmap bake complete");
    }
    ImGui::EndDisabled();

    if (m_BakeProgress >= 0.0f) {
        ImGui::ProgressBar(m_BakeProgress, ImVec2(-1, 0));
    }
    if (!m_BakeStatus.empty()) ImGui::TextDisabled("%s", m_BakeStatus.c_str());

    ImGui::End();
}

} // namespace fw
