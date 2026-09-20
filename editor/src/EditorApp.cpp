#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include "engine/lightmap/Lightmapper.h"
#include "engine/brush/Brush.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace fw {

bool EditorApp::OnInit() {
    m_Window.SetTitle("Forgeworks Map Maker");
    m_Engine.Init(false);
    m_AssetDatabase.Scan("assets");

    if (fs::exists(m_CurrentScenePath)) {
        m_Engine.LoadScene(m_CurrentScenePath);
    } else {
        m_Engine.NewScene("Untitled");
        // Seed a fresh scene with a camera + directional light + sky + ground plane
        // so the editor never opens to a totally empty void.
        Entity cam = m_Engine.GetScene().CreateEntity("Main Camera");
        cam.Get<TransformComponent>().local.position = vec3(0, 2, 6);
        cam.AddOrReplace<CameraComponent>();

        Entity sun = m_Engine.GetScene().CreateEntity("Sun");
        sun.Get<TransformComponent>().local.SetEulerDegrees(vec3(-50, -30, 0));
        auto& light = sun.AddOrReplace<LightComponent>();
        light.type = LightType::Directional;
        light.intensity = 3.0f;
        light.isStatic = false;

        Entity sky = m_Engine.GetScene().CreateEntity("Sky");
        sky.AddOrReplace<SkyLightComponent>();
    }

    FW_LOG_INFO("Forgeworks Map Maker initialized");
    return true;
}

#if FW_PLATFORM_WINDOWS
void EditorApp::CreateViewportTarget(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (width == m_ViewportTexWidth && height == m_ViewportTexHeight && m_ViewportSRV) return;

    m_ViewportRTV.Reset(); m_ViewportSRV.Reset(); m_ViewportColorTex.Reset();
    m_ViewportDSV.Reset(); m_ViewportDepthTex.Reset();

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = width; colorDesc.Height = height;
    colorDesc.MipLevels = 1; colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    m_Device.Device()->CreateTexture2D(&colorDesc, nullptr, &m_ViewportColorTex);
    m_Device.Device()->CreateRenderTargetView(m_ViewportColorTex.Get(), nullptr, &m_ViewportRTV);
    m_Device.Device()->CreateShaderResourceView(m_ViewportColorTex.Get(), nullptr, &m_ViewportSRV);

    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_Device.Device()->CreateTexture2D(&depthDesc, nullptr, &m_ViewportDepthTex);
    m_Device.Device()->CreateDepthStencilView(m_ViewportDepthTex.Get(), nullptr, &m_ViewportDSV);

    m_ViewportTexWidth = width;
    m_ViewportTexHeight = height;
}
#endif

RenderCamera EditorApp::BuildEditorCamera() const {
    RenderCamera rc;
    rc.position = m_CamPos;
    vec3 fwd;
    fwd.x = std::cos(Radians(m_CamYaw)) * std::cos(Radians(m_CamPitch));
    fwd.y = std::sin(Radians(m_CamPitch));
    fwd.z = std::sin(Radians(m_CamYaw)) * std::cos(Radians(m_CamPitch));
    fwd = glm::normalize(fwd);
    rc.view = glm::lookAt(m_CamPos, m_CamPos + fwd, vec3(0, 1, 0));
    float aspect = m_ViewportSize.y > 0 ? m_ViewportSize.x / m_ViewportSize.y : 1.7f;
    rc.proj = glm::perspective(Radians(60.0f), aspect, 0.05f, 2000.0f);
    return rc;
}

void EditorApp::UpdateEditorCamera(float dt) {
    if (!m_ViewportHovered && !m_ViewportFocused) return;
    Input& input = Input::Get();

    if (input.IsMouseButtonDown(1)) { // RMB: fly camera, Source/Unreal style
        vec2 delta = input.MouseDelta();
        m_CamYaw += delta.x * 0.15f;
        m_CamPitch = glm::clamp(m_CamPitch - delta.y * 0.15f, -89.0f, 89.0f);

        vec3 fwd(std::cos(Radians(m_CamYaw)) * std::cos(Radians(m_CamPitch)),
                 std::sin(Radians(m_CamPitch)),
                 std::sin(Radians(m_CamYaw)) * std::cos(Radians(m_CamPitch)));
        fwd = glm::normalize(fwd);
        vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
        vec3 up(0, 1, 0);

        float speed = m_CamSpeed * (input.IsKeyDown(VK_SHIFT) ? 3.0f : 1.0f) * dt;
        if (input.IsKeyDown('W')) m_CamPos += fwd * speed;
        if (input.IsKeyDown('S')) m_CamPos -= fwd * speed;
        if (input.IsKeyDown('A')) m_CamPos -= right * speed;
        if (input.IsKeyDown('D')) m_CamPos += right * speed;
        if (input.IsKeyDown('E') || input.IsKeyDown(VK_SPACE)) m_CamPos += up * speed;
        if (input.IsKeyDown('Q') || input.IsKeyDown(VK_CONTROL)) m_CamPos -= up * speed;
    }
}

void EditorApp::OnUpdate(float dt) {
    Input::Get().SetCursorLocked(false);

    if (m_Mode == EditorMode::Hammer) UpdateHammerMode(dt);
    UpdateEditorCamera(dt);

    m_Engine.Tick(m_Engine.State() == EngineRunState::Playing ? dt : 0.0f);
    m_Engine.GetScene().UpdateTransforms();

    static float pollTimer = 0.0f;
    pollTimer += dt;
    if (pollTimer > 1.0f) {
        pollTimer = 0.0f;
        m_AssetDatabase.PollForChanges([this](const AssetEntry& e) {
            if (e.type == AssetType::Script && m_Engine.State() == EngineRunState::Playing) {
                m_Engine.GetScriptEngine().ReloadScript(e.path);
            }
        });
    }
}

void EditorApp::OnRender() {
#if FW_PLATFORM_WINDOWS
    CreateViewportTarget((int)m_ViewportSize.x, (int)m_ViewportSize.y);
    if (!m_ViewportRTV) return;

    RenderCamera cam = BuildEditorCamera();
    if (m_Engine.State() == EngineRunState::Playing) {
        Entity primaryCam = m_Engine.GetScene().PrimaryCamera();
        if (primaryCam) {
            auto& tc = primaryCam.Get<TransformComponent>();
            auto& cc = primaryCam.Get<CameraComponent>();
            cam.position = vec3(tc.worldMatrix[3]);
            cam.view = glm::inverse(tc.worldMatrix);
            float aspect = m_ViewportSize.y > 0 ? m_ViewportSize.x / m_ViewportSize.y : 1.7f;
            cam.proj = glm::perspective(Radians(cc.fovDeg), aspect, cc.nearClip, cc.farClip);
        }
    }

    RenderSettings settings;
    auto skyView = m_Engine.GetScene().Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = m_Engine.GetScene().Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }

    m_Renderer->RenderScene(m_Engine.GetScene(), cam, settings, m_ViewportRTV.Get(), m_ViewportDSV.Get(),
                             m_ViewportTexWidth, m_ViewportTexHeight);
#endif
}

void EditorApp::OnResize(int width, int height) { FW_UNUSED(width); FW_UNUSED(height); }

void EditorApp::OnShutdown() {
    m_Engine.Shutdown();
}

void EditorApp::NewScene() {
    m_Engine.NewScene("Untitled");
    m_SelectedEntity = entt::null;
}

void EditorApp::OpenScene(const std::string& path) {
    if (m_Engine.LoadScene(path)) {
        m_CurrentScenePath = path;
        m_SelectedEntity = entt::null;
    }
}

void EditorApp::SaveScene() { SaveSceneAs(m_CurrentScenePath); }

void EditorApp::SaveSceneAs(const std::string& path) {
    m_CurrentScenePath = path;
    m_Engine.SaveScene(path);
}

void EditorApp::OnImGui() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("EditorDockspaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    DrawMenuBar();

    ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    DrawToolbar();
    DrawViewportPanel();
    DrawHierarchyPanel();
    DrawInspectorPanel();
    DrawAssetBrowserPanel();
    DrawConsolePanel();
    DrawScriptEditorPanel();
    if (m_Mode == EditorMode::Hammer) DrawHammerToolPanel();
    DrawLightmapBakePanel();

    if (m_ShowDemoWindow) ImGui::ShowDemoWindow(&m_ShowDemoWindow);
}

void EditorApp::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) NewScene();
            if (ImGui::MenuItem("Open Scene...")) OpenScene(m_CurrentScenePath);
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem("Save As...")) SaveSceneAs(m_CurrentScenePath);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) m_Running = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Preferences...")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Create Empty")) m_Engine.GetScene().CreateEntity("Entity");
            if (ImGui::MenuItem("Camera")) m_Engine.GetScene().CreateEntity("Camera").AddOrReplace<CameraComponent>();
            if (ImGui::MenuItem("Directional Light")) {
                Entity e = m_Engine.GetScene().CreateEntity("Directional Light");
                e.AddOrReplace<LightComponent>().type = LightType::Directional;
            }
            if (ImGui::MenuItem("Point Light")) {
                Entity e = m_Engine.GetScene().CreateEntity("Point Light");
                e.AddOrReplace<LightComponent>().type = LightType::Point;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void EditorApp::DrawToolbar() {
    ImGui::Begin("Toolbar");

    if (m_Engine.State() == EngineRunState::Editing) {
        if (ImGui::Button(" Play ")) m_Engine.Play();
    } else {
        ImGui::BeginDisabled(m_Engine.State() == EngineRunState::Editing);
        if (ImGui::Button(" Stop ")) m_Engine.Stop();
        ImGui::SameLine();
        bool paused = m_Engine.State() == EngineRunState::Paused;
        if (ImGui::Checkbox("Pause", &paused)) m_Engine.SetPaused(paused);
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();

    bool hammer = (m_Mode == EditorMode::Hammer);
    ImGui::PushStyleColor(ImGuiCol_Button, hammer ? ImVec4(0.85f, 0.5f, 0.1f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button(hammer ? "[ Hammer Mode: ON ]" : "[ Enter Hammer Mode ]")) {
        m_Mode = hammer ? EditorMode::Scene : EditorMode::Hammer;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();
    ImGui::Text("Gizmo:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Translate", m_GizmoOperation == 0)) m_GizmoOperation = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", m_GizmoOperation == 1)) m_GizmoOperation = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", m_GizmoOperation == 2)) m_GizmoOperation = 2;

    ImGui::End();
}

void EditorApp::DrawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 size = ImGui::GetContentRegionAvail();
    m_ViewportSize = vec2(std::max(size.x, 64.0f), std::max(size.y, 64.0f));
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    m_ViewportPos = vec2(screenPos.x, screenPos.y);

#if FW_PLATFORM_WINDOWS
    if (m_ViewportSRV) {
        ImGui::Image((ImTextureID)(intptr_t)m_ViewportSRV.Get(), size);
    }
#endif

    // Gizmo for the selected entity
    if (m_SelectedEntity != entt::null && m_Engine.GetScene().Registry().valid(m_SelectedEntity)) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(screenPos.x, screenPos.y, size.x, size.y);

        RenderCamera cam = BuildEditorCamera();
        Entity sel(m_SelectedEntity, &m_Engine.GetScene().Registry());
        auto& tc = sel.Get<TransformComponent>();
        mat4 world = tc.worldMatrix;

        ImGuizmo::OPERATION op = m_GizmoOperation == 0 ? ImGuizmo::TRANSLATE : (m_GizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
        if (ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(cam.proj), op, ImGuizmo::WORLD, glm::value_ptr(world))) {
            Transform newLocal = Transform::FromMatrix(world);
            tc.local = newLocal;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::DrawEntityNode(entt::entity e) {
    Entity entity(e, &m_Engine.GetScene().Registry());
    if (!entity.IsValid()) return;
    auto& hierarchy = entity.Get<HierarchyComponent>();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_SelectedEntity == e) flags |= ImGuiTreeNodeFlags_Selected;
    if (hierarchy.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)e, flags, "%s", entity.Name().c_str());
    if (ImGui::IsItemClicked()) m_SelectedEntity = e;

    if (!hierarchy.children.empty() && opened) {
        for (auto child : hierarchy.children) DrawEntityNode(child);
        ImGui::TreePop();
    }
}

void EditorApp::DrawHierarchyPanel() {
    ImGui::Begin("Hierarchy");
    if (ImGui::Button("+ Create Entity")) m_SelectedEntity = m_Engine.GetScene().CreateEntity("Entity").Handle();
    ImGui::SameLine();
    if (ImGui::Button("Delete") && m_SelectedEntity != entt::null) {
        m_Engine.GetScene().DestroyEntity(Entity(m_SelectedEntity, &m_Engine.GetScene().Registry()));
        m_SelectedEntity = entt::null;
    }
    ImGui::Separator();

    m_Engine.GetScene().Each<HierarchyComponent>([this](Entity e, HierarchyComponent& h) {
        if (h.parent == entt::null) DrawEntityNode(e.Handle());
    });
    ImGui::End();
}

} // namespace fw
