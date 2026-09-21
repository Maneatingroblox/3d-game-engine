// Inspector panel: shows/edits every component on the selected entity, plus
// "Add Component" - the Godot/Unity-style property inspector. Scripts are
// edited by path + exported Properties table (string key/value, matching
// ScriptComponent::properties), which the Lua side reads as `self.Properties.X`.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <fstream>

namespace fw {

static void DrawVec3(const char* label, vec3& v, float speed = 0.1f) {
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine(120);
    ImGui::PushItemWidth(60);
    ImGui::DragFloat("X", &v.x, speed); ImGui::SameLine();
    ImGui::DragFloat("Y", &v.y, speed); ImGui::SameLine();
    ImGui::DragFloat("Z", &v.z, speed);
    ImGui::PopItemWidth();
    ImGui::PopID();
}

void EditorApp::DrawInspectorPanel() {
    ImGui::Begin("Inspector");

    if (m_SelectedEntity == entt::null || !m_Engine.GetScene().Registry().valid(m_SelectedEntity)) {
        ImGui::TextDisabled("No entity selected.");
        ImGui::End();
        return;
    }

    Entity entity(m_SelectedEntity, &m_Engine.GetScene().Registry());

    // Any edit in this panel changes the rendered image; the CPU preview uses
    // that flag to decide whether it must re-render.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) m_SoftwarePreviewDirty = true;

    // Name
    std::string name = entity.Name();
    if (ImGui::InputText("Name", &name)) entity.SetName(name);
    ImGui::Separator();

    // Transform
    if (entity.Has<TransformComponent>() && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& tc = entity.Get<TransformComponent>();
        DrawVec3("Position", tc.local.position);
        vec3 euler = tc.local.EulerDegrees();
        if (ImGui::IsItemDeactivatedAfterEdit()) {}
        DrawVec3("Rotation", euler, 1.0f);
        tc.local.SetEulerDegrees(euler);
        DrawVec3("Scale", tc.local.scale, 0.05f);
    }

    // Camera
    if (entity.Has<CameraComponent>()) {
        bool open = ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<CameraComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<CameraComponent>()) {
            auto& cc = entity.Get<CameraComponent>();
            ImGui::Checkbox("Primary", &cc.isPrimary);
            ImGui::DragFloat("FOV", &cc.fovDeg, 0.5f, 10.0f, 170.0f);
            ImGui::DragFloat("Near Clip", &cc.nearClip, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far Clip", &cc.farClip, 1.0f, 1.0f, 10000.0f);
            ImGui::Checkbox("Orthographic", &cc.orthographic);
        }
    }

    // Light
    if (entity.Has<LightComponent>()) {
        bool open = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<LightComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<LightComponent>()) {
            auto& lc = entity.Get<LightComponent>();
            const char* types[] = { "Directional", "Point", "Spot" };
            int t = (int)lc.type;
            if (ImGui::Combo("Type", &t, types, 3)) lc.type = (LightType)t;
            ImGui::ColorEdit3("Color", &lc.color.x);
            ImGui::DragFloat("Intensity", &lc.intensity, 0.05f, 0.0f, 100.0f);
            if (lc.type != LightType::Directional) ImGui::DragFloat("Range", &lc.range, 0.1f, 0.1f, 500.0f);
            if (lc.type == LightType::Spot) {
                ImGui::DragFloat("Inner Cone", &lc.innerConeDeg, 0.5f, 0.0f, 89.0f);
                ImGui::DragFloat("Outer Cone", &lc.outerConeDeg, 0.5f, 0.0f, 89.0f);
            }
            ImGui::Checkbox("Casts Shadows", &lc.castsShadows);
            ImGui::Checkbox("Static (bake into lightmap)", &lc.isStatic);
        }
    }

    // Mesh Renderer
    if (entity.Has<MeshRendererComponent>()) {
        bool open = ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<MeshRendererComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<MeshRendererComponent>()) {
            auto& mr = entity.Get<MeshRendererComponent>();
            ImGui::InputText("Mesh Asset", &mr.meshAsset);
            ImGui::Checkbox("Cast Shadows", &mr.castShadows);
            ImGui::Checkbox("Receive Shadows", &mr.receiveShadows);
            ImGui::Checkbox("Use Lightmap", &mr.useLightmap);
            if (mr.useLightmap) ImGui::InputText("Lightmap Asset", &mr.lightmapAsset);
            ImGui::Text("Material Slots: %d", (int)mr.materialSlots.size());
            for (size_t i = 0; i < mr.materialSlots.size(); ++i) {
                ImGui::PushID((int)i);
                ImGui::InputText("##mat", &mr.materialSlots[i]);
                ImGui::PopID();
            }
            if (ImGui::Button("+ Material Slot")) mr.materialSlots.push_back("assets/materials/dev_grey.fwmat");
        }
    }

    // Sky Light
    if (entity.Has<SkyLightComponent>()) {
        bool open = ImGui::CollapsingHeader("Sky Light", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<SkyLightComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<SkyLightComponent>()) {
            auto& sky = entity.Get<SkyLightComponent>();
            ImGui::ColorEdit3("Ambient Color", &sky.ambientColor.x);
            ImGui::DragFloat("Ambient Intensity", &sky.ambientIntensity, 0.05f, 0.0f, 10.0f);
            ImGui::InputText("Skybox Asset", &sky.skyboxAsset);
        }
    }

    // Rigid Body
    if (entity.Has<RigidBodyComponent>()) {
        bool open = ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<RigidBodyComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<RigidBodyComponent>()) {
            auto& rb = entity.Get<RigidBodyComponent>();
            const char* motions[] = { "Static", "Kinematic", "Dynamic" };
            int m = (int)rb.motionType;
            if (ImGui::Combo("Motion Type", &m, motions, 3)) rb.motionType = (BodyMotionType)m;
            ImGui::DragFloat("Mass", &rb.mass, 0.1f, 0.01f, 10000.0f);
            ImGui::DragFloat("Friction", &rb.friction, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Restitution", &rb.restitution, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Is Trigger", &rb.isTrigger);
            ImGui::Checkbox("Gravity Enabled", &rb.gravityEnabled);
        }
    }

    // Collider
    if (entity.Has<ColliderComponent>()) {
        bool open = ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<ColliderComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<ColliderComponent>()) {
            auto& col = entity.Get<ColliderComponent>();
            const char* shapes[] = { "Box", "Sphere", "Capsule", "ConvexHull", "TriangleMesh" };
            int s = (int)col.shape;
            if (ImGui::Combo("Shape", &s, shapes, 5)) col.shape = (ColliderShape)s;
            if (col.shape == ColliderShape::Box) DrawVec3("Half Extents", col.halfExtents, 0.05f);
            if (col.shape == ColliderShape::Sphere || col.shape == ColliderShape::Capsule)
                ImGui::DragFloat("Radius", &col.radius, 0.05f, 0.01f, 100.0f);
            if (col.shape == ColliderShape::Capsule)
                ImGui::DragFloat("Height", &col.height, 0.05f, 0.01f, 100.0f);
            if (col.shape == ColliderShape::ConvexHull || col.shape == ColliderShape::TriangleMesh)
                ImGui::InputText("Collision Mesh", &col.collisionMeshAsset);
        }
    }

    // Audio Source
    if (entity.Has<AudioSourceComponent>()) {
        bool open = ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<AudioSourceComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<AudioSourceComponent>()) {
            auto& as = entity.Get<AudioSourceComponent>();
            ImGui::InputText("Sound Asset", &as.soundAsset);
            ImGui::Checkbox("3D", &as.is3D);
            ImGui::Checkbox("Loop", &as.loop);
            ImGui::Checkbox("Play On Start", &as.playOnStart);
            ImGui::SliderFloat("Volume", &as.volume, 0.0f, 1.0f);
            ImGui::SliderFloat("Pitch", &as.pitch, 0.1f, 3.0f);
            ImGui::DragFloat("Min Distance", &as.minDistance, 0.1f, 0.0f, 1000.0f);
            ImGui::DragFloat("Max Distance", &as.maxDistance, 0.1f, 0.0f, 1000.0f);
        }
    }

    // Script
    if (entity.Has<ScriptComponent>()) {
        bool open = ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) entity.Remove<ScriptComponent>();
            ImGui::EndPopup();
        }
        if (open && entity.Has<ScriptComponent>()) {
            auto& sc = entity.Get<ScriptComponent>();
            if (ImGui::InputText("Script Asset", &sc.scriptAsset, ImGuiInputTextFlags_EnterReturnsTrue)) {
                m_Engine.GetScriptEngine().AttachScript(entity.Handle(), sc.scriptAsset);
            }
            if (ImGui::SmallButton("Open in Script Editor") && !sc.scriptAsset.empty())
                LoadScriptIntoEditor(sc.scriptAsset);
            ImGui::Checkbox("Enabled", &sc.enabled);
            ImGui::Separator();
            ImGui::TextDisabled("Exported Properties:");
            for (auto& [key, value] : sc.properties) {
                ImGui::PushID(key.c_str());
                ImGui::Text("%s", key.c_str());
                ImGui::SameLine(150);
                std::string v = value;
                if (ImGui::InputText("##val", &v)) value = v;
                ImGui::PopID();
            }
            static std::string newPropKey;
            ImGui::InputText("##newkey", &newPropKey);
            ImGui::SameLine();
            if (ImGui::Button("+ Property") && !newPropKey.empty()) { sc.properties[newPropKey] = ""; newPropKey.clear(); }
        }
    }

    // Brush (Hammer-mode geometry; view-only info here, editing happens in Hammer mode)
    if (entity.Has<BrushComponent>()) {
        if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& bc = entity.Get<BrushComponent>();
            ImGui::Text("Brush ID: %llu", (unsigned long long)(u64)bc.brushId);
            ImGui::Checkbox("Is Trigger", &bc.isTrigger);
            ImGui::InputText("Classname", &bc.classname);
            ImGui::TextDisabled("Switch to Hammer Mode to edit geometry.");
        }
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponentPopup");
    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!entity.Has<CameraComponent>() && ImGui::MenuItem("Camera")) entity.AddOrReplace<CameraComponent>();
        if (!entity.Has<LightComponent>() && ImGui::MenuItem("Light")) entity.AddOrReplace<LightComponent>();
        if (!entity.Has<MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer")) entity.AddOrReplace<MeshRendererComponent>();
        if (!entity.Has<SkyLightComponent>() && ImGui::MenuItem("Sky Light")) entity.AddOrReplace<SkyLightComponent>();
        if (!entity.Has<RigidBodyComponent>() && ImGui::MenuItem("Rigid Body")) entity.AddOrReplace<RigidBodyComponent>();
        if (!entity.Has<ColliderComponent>() && ImGui::MenuItem("Collider")) entity.AddOrReplace<ColliderComponent>();
        if (!entity.Has<CharacterControllerComponent>() && ImGui::MenuItem("Character Controller")) entity.AddOrReplace<CharacterControllerComponent>();
        if (!entity.Has<AudioSourceComponent>() && ImGui::MenuItem("Audio Source")) entity.AddOrReplace<AudioSourceComponent>();
        if (!entity.Has<AudioListenerComponent>() && ImGui::MenuItem("Audio Listener")) entity.AddOrReplace<AudioListenerComponent>();
        if (!entity.Has<ScriptComponent>() && ImGui::MenuItem("Script")) entity.AddOrReplace<ScriptComponent>();
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace fw
