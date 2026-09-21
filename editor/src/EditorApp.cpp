#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/platform/Input.h"
#include "engine/lightmap/Lightmapper.h"
#include "engine/brush/Brush.h"
#include "engine/scene/DefaultScene.h"
#include "engine/scene/SceneSerializer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <filesystem>
#include <fstream>
#include <chrono>

// stb_image_write's implementation is compiled into fw_engine (Lightmapper.cpp);
// the editor only needs the declarations for CaptureViewportScreenshot().
#include <stb_image_write.h>

namespace fs = std::filesystem;

namespace fw {

bool EditorApp::OnInit() {
    // Content paths are project-relative ("assets/..."); make sure they resolve
    // even when the editor is launched from build/bin or a shortcut.
    Paths::Initialize();
    FW_LOG_INFO("Project root: %s (from %s)", Paths::ProjectRoot().c_str(), Paths::RootSource().c_str());

    m_Window.SetTitle("Forgeworks Map Maker");
    m_Engine.Init(false);
    m_AssetDatabase.Scan("assets");

    // The editor must never open on an empty void: make sure a starter scene
    // exists on disk (camera + sun + sky + ground + a few primitives) and load
    // it. If loading fails for any reason, build the same scene in memory.
    m_CurrentScenePath = m_StartupScenePath.empty() ? DefaultScene::DefaultScenePath() : m_StartupScenePath;
    const std::string resolvedScene = Paths::Resolve(m_CurrentScenePath);
    DefaultScene::EnsureStarterContent(m_CurrentScenePath);

    if (!fs::exists(resolvedScene) || !m_Engine.LoadScene(resolvedScene)) {
        FW_LOG_WARN("Could not load '%s' - creating a new starter scene in memory", resolvedScene.c_str());
        NewScene();
    }

    if (m_AssetDatabase.Entries().empty())
        FW_LOG_WARN("No assets found under '%s' - the viewport will still draw its grid.", Paths::AssetRoot().c_str());

    ResetEditorCamera();
    // Start from the scene's own camera framing when there is one, so the first
    // frame shows the level as authored instead of an arbitrary viewpoint.
    if (Entity primary = m_Engine.GetScene().PrimaryCamera()) {
        auto& tc = primary.Get<TransformComponent>();
        m_CamPos = tc.local.position;
        const vec3 euler = tc.local.EulerDegrees();
        m_CamPitch = euler.x;
        m_CamYaw = euler.y;
    }
    FW_LOG_INFO("Forgeworks Map Maker initialized");
    return true;
}

void EditorApp::ResetEditorCamera() {
    m_CamPos = vec3(9.0f, 6.0f, 12.0f);
    m_CamYaw = 36.0f;
    m_CamPitch = -22.0f;
    m_CamSpeed = 8.0f;
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
    HRESULT hr = m_Device.Device()->CreateTexture2D(&colorDesc, nullptr, &m_ViewportColorTex);
    if (FAILED(hr)) { FW_LOG_ERROR("Failed to create viewport colour target (0x%08lX)", hr); return; }
    m_Device.Device()->CreateRenderTargetView(m_ViewportColorTex.Get(), nullptr, &m_ViewportRTV);
    m_Device.Device()->CreateShaderResourceView(m_ViewportColorTex.Get(), nullptr, &m_ViewportSRV);

    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_Device.Device()->CreateTexture2D(&depthDesc, nullptr, &m_ViewportDepthTex);
    if (FAILED(hr)) { FW_LOG_ERROR("Failed to create viewport depth target (0x%08lX)", hr); return; }
    m_Device.Device()->CreateDepthStencilView(m_ViewportDepthTex.Get(), nullptr, &m_ViewportDSV);

    m_ViewportTexWidth = width;
    m_ViewportTexHeight = height;
}

void EditorApp::CreateSoftwarePreviewTexture(const SoftwareImage& image) {
    if (image.Empty()) return;
    if (m_PreviewTex && m_PreviewTexWidth == image.width && m_PreviewTexHeight == image.height) {
        m_Device.Context()->UpdateSubresource(m_PreviewTex.Get(), 0, nullptr, image.Data(),
                                              (UINT)image.width * 4u, 0);
        return;
    }

    ReleaseSoftwarePreviewTexture();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)image.width;
    desc.Height = (UINT)image.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_Device.Device()->CreateTexture2D(&desc, nullptr, &m_PreviewTex))) {
        FW_LOG_ERROR("Failed to create software-preview texture");
        return;
    }
    m_Device.Context()->UpdateSubresource(m_PreviewTex.Get(), 0, nullptr, image.Data(),
                                          (UINT)image.width * 4u, 0);
    m_Device.Device()->CreateShaderResourceView(m_PreviewTex.Get(), nullptr, &m_PreviewSRV);
    m_PreviewTexWidth = image.width;
    m_PreviewTexHeight = image.height;
}

void EditorApp::ReleaseSoftwarePreviewTexture() {
    m_PreviewTex.Reset();
    m_PreviewSRV.Reset();
    m_PreviewTexWidth = m_PreviewTexHeight = 0;
}
#endif

RenderCamera EditorApp::BuildEditorCamera() const {
    const float aspect = m_ViewportSize.y > 0.5f ? m_ViewportSize.x / m_ViewportSize.y : 16.0f / 9.0f;
    return MakeCamera(m_CamPos, m_CamYaw, m_CamPitch, 60.0f, aspect, 0.05f, 2000.0f);
}

void EditorApp::UpdateEditorCamera(float dt) {
    Input& input = Input::Get();

    // Mouse wheel adjusts the fly speed (Hammer/Unreal style), so a "stuck"
    // camera is easy to escape.
    if (m_ViewportHovered && std::abs(input.WheelDelta()) > 0.0f) {
        m_CamSpeed = glm::clamp(m_CamSpeed * std::pow(1.15f, input.WheelDelta()), 0.1f, 500.0f);
    }

    if (!m_ViewportHovered && !m_ViewportFocused) return;

    const bool fly = input.IsMouseButtonDown(1); // RMB: fly camera, Source/Unreal style
    if (fly) {
        // Dragging right turns the view right; yaw decreases because a
        // positive yaw rotation about +Y turns towards -X.
        const vec2 delta = input.MouseDelta();
        m_CamYaw -= delta.x * 0.15f;
        m_CamPitch = glm::clamp(m_CamPitch - delta.y * 0.15f, -89.0f, 89.0f);
    }

    vec3 fwd = YawPitchForward(m_CamYaw, m_CamPitch);
    const vec3 right = glm::normalize(glm::cross(fwd, vec3(0, 1, 0)));
    const vec3 up(0, 1, 0);

    float speed = m_CamSpeed * (input.IsKeyDown(VK_SHIFT) ? 3.0f : 1.0f) * dt;
    vec3 move(0.0f);
    if (input.IsKeyDown('W')) move += fwd;
    if (input.IsKeyDown('S')) move -= fwd;
    if (input.IsKeyDown('A')) move -= right;
    if (input.IsKeyDown('D')) move += right;
    if (input.IsKeyDown('E') || input.IsKeyDown(VK_SPACE)) move += up;
    if (input.IsKeyDown('Q') || input.IsKeyDown(VK_CONTROL)) move -= up;

    // Movement is allowed with or without RMB held (WASD navigation should not
    // require a mouse button that the user may not realise is the "fly" key).
    if (glm::length2(move) > 0.0f) {
        m_CamPos += glm::normalize(move) * speed;
        m_SoftwarePreviewDirty = true;
    }

    if (input.WasKeyPressed('F')) m_RequestFrameSelection = true;
}

void EditorApp::FrameSelection() {
    if (m_SelectedEntity == entt::null || !m_Engine.GetScene().Registry().valid(m_SelectedEntity)) return;
    Entity sel(m_SelectedEntity, &m_Engine.GetScene().Registry());
    if (!sel.Has<TransformComponent>()) return;

    const vec3 center(sel.Get<TransformComponent>().worldMatrix[3]);
    const vec3 dir = YawPitchForward(m_CamYaw, m_CamPitch);
    m_CamPos = center - dir * 8.0f;
    FW_LOG_INFO("Framed '%s'", sel.Name().c_str());
}

void EditorApp::OnUpdate(float dt) {
    Input::Get().SetCursorLocked(false);

    if (m_RequestFrameSelection) {
        m_RequestFrameSelection = false;
        FrameSelection();
    }

    if (m_Mode == EditorMode::Hammer) UpdateHammerMode(dt);
    UpdateEditorCamera(dt);

    m_Engine.Tick(dt);
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
    const int vpWidth = (int)m_ViewportSize.x;
    const int vpHeight = (int)m_ViewportSize.y;
    if (vpWidth <= 0 || vpHeight <= 0) return;

    if (m_SoftwarePreview) {
        // CPU path: nothing to do here (the image is uploaded in OnImGui).
        return;
    }

    CreateViewportTarget(vpWidth, vpHeight);
    if (!m_ViewportRTV) {
        // No render target (device lost / creation failed): let the CPU preview
        // take over rather than showing an empty panel.
        if (!m_SoftwarePreview) {
            FW_LOG_WARN("GPU viewport target unavailable - switching to the CPU viewport preview");
            m_SoftwarePreview = true;
            m_SoftwarePreviewDirty = true;
        }
        return;
    }

    // If the HLSL shaders can't be compiled (missing files, syntax error, wrong
    // working directory), rendering would silently produce an empty viewport.
    // Detect it once and fall back to the CPU preview, with a log line saying
    // exactly what happened.
    if (!m_SoftwarePreview && !m_Renderer->MeshShaderAvailable()) {
        FW_LOG_ERROR("Mesh shader unavailable - switching the viewport to the CPU preview "
                     "(see the Console for the shader compile error)");
        m_SoftwarePreview = true;
        m_SoftwarePreviewDirty = true;
        return;
    }

    RenderCamera cam = BuildEditorCamera();
    if (m_Engine.State() != EngineRunState::Editing) {
        Entity primaryCam = m_Engine.GetScene().PrimaryCamera();
        if (primaryCam) {
            auto& tc = primaryCam.Get<TransformComponent>();
            auto& cc = primaryCam.Get<CameraComponent>();
            cam.position = vec3(tc.worldMatrix[3]);
            cam.view = glm::inverse(tc.worldMatrix);
            const float aspect = vpHeight > 0 ? (float)vpWidth / (float)vpHeight : 16.0f / 9.0f;
            cam.proj = MakeProjectionMatrix(cc.fovDeg, aspect, cc.nearClip, cc.farClip);
        }
    }

    RenderSettings settings = m_RenderSettings;
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

void EditorApp::OnScreenshot(const std::string& path) {
    // Also write a viewport-only image (rendered by the CPU renderer, so it
    // works even when the GPU path is unavailable) next to the window capture:
    //   shots/editor.png  -> whole window
    //   shots/editor.viewport.png -> 3D viewport only
    const std::string viewportPath = path + ".viewport.png";
    CaptureViewportScreenshot(viewportPath);
}

void EditorApp::OnShutdown() {
#if FW_PLATFORM_WINDOWS
    ReleaseSoftwarePreviewTexture();
#endif
    m_Engine.Shutdown();
}

void EditorApp::NewScene() {
    m_Engine.NewScene("Default");
    DefaultScene::Build(m_Engine.GetScene(), &m_Engine.GetBrushMap());
    m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
    m_SelectedEntity = entt::null;
    m_SelectedBrush = UUID{0};
    ResetEditorCamera();
    FW_LOG_INFO("New scene created (starter scene)");
}

void EditorApp::OpenScene(const std::string& path) {
    const std::string resolved = Paths::Resolve(path);
    if (m_Engine.LoadScene(resolved)) {
        m_CurrentScenePath = path;
        m_SelectedEntity = entt::null;
        m_SelectedBrush = UUID{0};
        m_SoftwarePreviewDirty = true;
        FW_LOG_INFO("Opened scene: %s", resolved.c_str());
    } else {
        FW_LOG_ERROR("Failed to open scene: %s", resolved.c_str());
    }
}

void EditorApp::SaveScene() { SaveSceneAs(m_CurrentScenePath); }

void EditorApp::SaveSceneAs(const std::string& path) {
    m_CurrentScenePath = path;
    m_Engine.SaveScene(path);
}

// ---------------------------------------------------------------------------
// Software (CPU) viewport preview
// ---------------------------------------------------------------------------
void EditorApp::UpdateSoftwarePreview(int width, int height) {
    const int scale = std::max(1, (int)(1.0f / std::max(m_SoftwarePreviewScale, 0.05f)));
    const int w = std::max(64, width / scale);
    const int h = std::max(64, height / scale);
    if (!m_SoftwarePreviewDirty && m_SoftwareImage.width == w && m_SoftwareImage.height == h) return;

    m_SoftwareImage.Resize(w, h);

    RenderCamera cam = BuildEditorCamera();
    if (m_Engine.State() != EngineRunState::Editing) {
        if (Entity primaryCam = m_Engine.GetScene().PrimaryCamera()) {
            auto& tc = primaryCam.Get<TransformComponent>();
            auto& cc = primaryCam.Get<CameraComponent>();
            cam.position = vec3(tc.worldMatrix[3]);
            cam.view = glm::inverse(tc.worldMatrix);
            cam.proj = MakeProjectionMatrix(cc.fovDeg, (float)w / (float)h, cc.nearClip, cc.farClip);
        }
    }

    RenderSettings settings = m_RenderSettings;
    auto skyView = m_Engine.GetScene().Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = m_Engine.GetScene().Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }

    const auto start = std::chrono::high_resolution_clock::now();
    SoftwareRenderStats stats;
    SoftwareRenderer::RenderScene(m_Engine.GetScene(), cam, settings, m_SoftwareImage, &stats);
    m_LastSoftwareMs = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - start).count();

    m_SoftwarePreviewDirty = false;

#if FW_PLATFORM_WINDOWS
    CreateSoftwarePreviewTexture(m_SoftwareImage);
#endif
}

void EditorApp::DrawSoftwarePreviewImage(float width, float height) {
    const ImVec2 size(width, height);
#if FW_PLATFORM_WINDOWS
    if (m_PreviewSRV) ImGui::Image((ImTextureID)(intptr_t)m_PreviewSRV.Get(), size);
    else ImGui::TextDisabled("Software preview unavailable");
#else
    FW_UNUSED(size);
#endif
}

bool EditorApp::SaveViewportImage(const std::string& path, const SoftwareImage& image) {
    if (image.Empty()) return false;
    const std::string resolved = Paths::Resolve(path);
    std::error_code ec;
    if (fs::path(resolved).has_parent_path()) fs::create_directories(fs::path(resolved).parent_path(), ec);
    const int ok = stbi_write_png(resolved.c_str(), image.width, image.height, 4, image.Data(), image.width * 4);
    if (!ok) FW_LOG_ERROR("Failed to write screenshot: %s", resolved.c_str());
    else FW_LOG_INFO("Screenshot saved: %s (%dx%d)", resolved.c_str(), image.width, image.height);
    return ok != 0;
}

void EditorApp::CaptureViewportScreenshot(const std::string& path) {
    // Renders one frame at the viewport's resolution through the CPU renderer
    // and writes it to disk. This is how the editor verifies (and shows) what
    // the viewport contains without needing a GPU read-back.
    int w = (int)m_ViewportSize.x, h = (int)m_ViewportSize.y;
    if (w <= 1 || h <= 1) { w = 1280; h = 720; }

    SoftwareImage image;
    image.Resize(w, h);

    RenderCamera cam = BuildEditorCamera();
    if (m_Engine.State() != EngineRunState::Editing) {
        if (Entity primaryCam = m_Engine.GetScene().PrimaryCamera()) {
            auto& tc = primaryCam.Get<TransformComponent>();
            auto& cc = primaryCam.Get<CameraComponent>();
            cam.position = vec3(tc.worldMatrix[3]);
            cam.view = glm::inverse(tc.worldMatrix);
            cam.proj = MakeProjectionMatrix(cc.fovDeg, (float)w / (float)h, cc.nearClip, cc.farClip);
        }
    }

    RenderSettings settings = m_RenderSettings;
    auto skyView = m_Engine.GetScene().Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = m_Engine.GetScene().Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }

    SoftwareRenderStats stats;
    SoftwareRenderer::RenderScene(m_Engine.GetScene(), cam, settings, image, &stats);
    FW_LOG_INFO("Viewport capture: %d mesh entities, %d triangles, %.1f%% geometry coverage",
                stats.meshEntities, stats.drawnTriangles, stats.GeometryCoverage() * 100.0f);
    SaveViewportImage(path, image);
}

// ---------------------------------------------------------------------------
// ImGui
// ---------------------------------------------------------------------------
void EditorApp::BuildDefaultLayout(unsigned int dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize((ImGuiID)dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = (ImGuiID)dockspaceId;
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    ImGuiID bottomRight = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.5f, nullptr, &bottom);

    ImGui::DockBuilderDockWindow("Viewport", center);
    // The "Toolbar" window is intentionally left floating (DrawToolbar() pins it
    // to the top of the viewport every frame).
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Assets", bottom);
    ImGui::DockBuilderDockWindow("Console", bottomRight);
    ImGui::DockBuilderDockWindow("Lighting", right);
    ImGui::DockBuilderDockWindow("Hammer Tools", right);
    ImGui::DockBuilderDockWindow("Script Editor", bottom);

    ImGui::DockBuilderFinish((ImGuiID)dockspaceId);
    m_DefaultLayoutBuilt = true;
    FW_LOG_INFO("Dock layout created");
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
    if (!m_DefaultLayoutBuilt && ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        BuildDefaultLayout(dockspaceId);
    }
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

    // One-shot sanity check: the Viewport must be docked in a node of its own
    // and big enough to see. If a layout saved by an older build put it
    // somewhere invisible, rebuild the default layout instead of leaving the
    // user staring at an empty window.
    if (m_LayoutCheckCountdown > 0) {
        m_LayoutCheckCountdown--;
        if (m_LayoutCheckCountdown == 0) {
            ImGuiWindow* viewportWindow = ImGui::FindWindowByName("Viewport");
            const bool docked = viewportWindow && viewportWindow->DockNode != nullptr;
            const int tabs = (docked && viewportWindow->DockNode->TabBar)
                                 ? viewportWindow->DockNode->TabBar->Tabs.Size : 0;
            const bool visible = viewportWindow && viewportWindow->Size.x >= 64.0f && viewportWindow->Size.y >= 64.0f;
            if (!docked || !visible || tabs > 1) {
                FW_LOG_WARN("Viewport was not visible (docked=%d tabs=%d visible=%d) - rebuilding the dock layout",
                            docked ? 1 : 0, tabs, visible ? 1 : 0);
                BuildDefaultLayout(ImGui::GetID("EditorDockspace"));
            }
        }
    }
}

void EditorApp::DrawMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) NewScene();
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) OpenScene(m_CurrentScenePath);
            if (ImGui::MenuItem("Save", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem("Save As...")) SaveSceneAs(m_CurrentScenePath);
            ImGui::Separator();
            if (ImGui::MenuItem("Screenshot Viewport (PNG)", "F12")) CaptureViewportScreenshot("assets/screenshots/viewport.png");
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
            ImGui::Separator();
            if (ImGui::BeginMenu("Primitive")) {
                struct Prim { const char* label; const char* asset; };
                static const Prim prims[] = {
                    { "Cube", "builtin:cube" }, { "Sphere", "builtin:sphere" },
                    { "Plane", "builtin:plane" }, { "Cylinder", "builtin:cylinder" },
                    { "Ramp", "builtin:ramp" },
                };
                for (const auto& p : prims) {
                    if (ImGui::MenuItem(p.label)) {
                        // Placed in front of the editor camera so it is
                        // immediately visible (a primitive spawned at the
                        // origin can be off-screen).
                        const vec3 spawn = m_CamPos + YawPitchForward(m_CamYaw, m_CamPitch) * 6.0f;
                        Entity e = m_Engine.GetScene().CreateEntity(p.label);
                        e.Get<TransformComponent>().local.position = spawn;
                        auto& mr = e.AddOrReplace<MeshRendererComponent>();
                        mr.meshAsset = p.asset;
                        mr.materialSlots = { "assets/materials/dev_grey.fwmat" };
                        auto& col = e.AddOrReplace<ColliderComponent>();
                        col.halfExtents = vec3(0.5f);
                        if (std::string(p.asset) == "builtin:sphere") {
                            col.shape = ColliderShape::Sphere;
                            col.radius = 0.5f;
                        }
                        m_SelectedEntity = e.Handle();
                        m_SoftwarePreviewDirty = true;
                        FW_LOG_INFO("Created %s at (%.1f, %.1f, %.1f)", p.label, spawn.x, spawn.y, spawn.z);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Draw Grid", nullptr, &m_RenderSettings.drawGrid);
            ImGui::MenuItem("Wireframe", nullptr, &m_RenderSettings.wireframe);
            ImGui::MenuItem("Shadows", nullptr, &m_RenderSettings.enableShadows);
            ImGui::MenuItem("Viewport Info Overlay", nullptr, &m_ShowViewportOverlay);
            ImGui::Separator();
            ImGui::MenuItem("CPU Viewport Preview (no GPU)", nullptr, &m_SoftwarePreview);
            if (ImGui::SliderFloat("Preview Scale", &m_SoftwarePreviewScale, 0.2f, 1.0f, "%.2f")) m_SoftwarePreviewDirty = true;
            ImGui::DragFloat("Grid Cell Size", &m_RenderSettings.gridCellSize, 0.1f, 0.1f, 32.0f);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Camera")) ResetEditorCamera();
            if (ImGui::MenuItem("Frame Selection", "F")) m_RequestFrameSelection = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Rebuild Layout")) { m_DefaultLayoutBuilt = false; }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void EditorApp::DrawToolbar() {
    // A slim floating bar pinned to the top of the viewport area.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 30.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_NoDocking);

    if (m_Engine.State() == EngineRunState::Editing) {
        if (ImGui::Button(" Play ")) m_Engine.Play();
    } else {
        if (ImGui::Button(" Stop ")) m_Engine.Stop();
        ImGui::SameLine();
        bool paused = m_Engine.State() == EngineRunState::Paused;
        if (ImGui::Checkbox("Pause", &paused)) m_Engine.SetPaused(paused);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), m_Engine.State() == EngineRunState::Playing ? "PLAYING" : "PAUSED");
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    bool hammer = (m_Mode == EditorMode::Hammer);
    ImGui::PushStyleColor(ImGuiCol_Button, hammer ? ImVec4(0.85f, 0.5f, 0.1f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button(hammer ? "[ Hammer Mode: ON ]" : "[ Hammer Mode ]")) {
        m_Mode = hammer ? EditorMode::Scene : EditorMode::Hammer;
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
    ImGui::Text("Gizmo:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Move", m_GizmoOperation == 0)) m_GizmoOperation = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", m_GizmoOperation == 1)) m_GizmoOperation = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", m_GizmoOperation == 2)) m_GizmoOperation = 2;

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_RenderSettings.drawGrid);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Speed", &m_CamSpeed, 0.25f, 0.1f, 500.0f, "%.1f");

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
    if (ImGui::Button("Screenshot")) CaptureViewportScreenshot("assets/screenshots/viewport.png");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Renders the viewport on the CPU and writes assets/screenshots/viewport.png");

    ImGui::End();

    // Keyboard shortcuts that are not tied to a focused panel.
    Input& input = Input::Get();
    if (input.WasKeyPressed(VK_F12)) CaptureViewportScreenshot("assets/screenshots/viewport.png");
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().WantTextInput) m_RequestFrameSelection = true;
}

void EditorApp::DrawViewportOverlay() {
    if (!m_ShowViewportOverlay) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const vec2 originV(origin.x, origin.y);
    const vec2 mpos = Input::Get().MousePosition();
    const vec2 local = mpos - m_ViewportPos;

    char line[256];
    std::snprintf(line, sizeof(line), "%s  |  %s  |  %s  |  camera (%.1f, %.1f, %.1f)  speed %.1f  |  entities %d",
                  m_SoftwarePreview ? "CPU preview" : "GPU",
                  m_RenderSettings.wireframe ? "wireframe" : "shaded",
                  m_RenderSettings.drawGrid ? "grid on" : "grid off",
                  m_CamPos.x, m_CamPos.y, m_CamPos.z, m_CamSpeed,
                  (int)m_Engine.GetScene().Registry().view<IDComponent>().size());

    dl->AddRectFilled(origin, ImVec2(origin.x + ImGui::CalcTextSize(line).x + 12.0f, origin.y + 20.0f),
                      IM_COL32(0, 0, 0, 140));
    dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 3.0f), IM_COL32(220, 220, 220, 255), line);

    if (m_SoftwarePreview) {
        std::snprintf(line, sizeof(line), "CPU render %.1f ms - press Screenshot (or F12) to save a PNG", m_LastSoftwareMs);
        dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 22.0f), IM_COL32(180, 220, 180, 255), line);
    }

    if (local.x >= 0.0f && local.y >= 0.0f && local.x < m_ViewportSize.x && local.y < m_ViewportSize.y) {
        std::snprintf(line, sizeof(line), "mouse %.0f, %.0f", local.x, local.y);
        dl->AddText(ImVec2(origin.x + 6.0f, origin.y + m_ViewportSize.y - 20.0f), IM_COL32(200, 200, 200, 255), line);
    }
}

void EditorApp::DrawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    m_ViewportFocused = ImGui::IsWindowFocused();
    m_ViewportHovered = ImGui::IsWindowHovered();

    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 64.0f) size.x = 64.0f;
    if (size.y < 64.0f) size.y = 64.0f;
    const vec2 previousSize = m_ViewportSize;
    m_ViewportSize = vec2(size.x, size.y);
    if (previousSize != m_ViewportSize) m_SoftwarePreviewDirty = true;

    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    m_ViewportPos = vec2(screenPos.x, screenPos.y);

#if FW_PLATFORM_WINDOWS
    if (m_SoftwarePreview) {
        UpdateSoftwarePreview((int)size.x, (int)size.y);
        DrawSoftwarePreviewImage(size.x, size.y);
    } else if (m_ViewportSRV) {
        // NOTE: ImGui::Image() advances the cursor, so the overlay is drawn
        // afterwards at the image's top-left corner.
        ImGui::Image((ImTextureID)(intptr_t)m_ViewportSRV.Get(), size);
    } else {
        ImGui::TextDisabled("Viewport render target unavailable - switch to View > CPU Viewport Preview.");
    }
#else
    UpdateSoftwarePreview((int)size.x, (int)size.y);
    DrawSoftwarePreviewImage(size.x, size.y);
#endif

    // Gizmo for the selected entity. The projection/view used here must be the
    // same camera the scene was rendered with, otherwise the manipulator drifts
    // away from the object it is supposed to move.
    if (m_SelectedEntity != entt::null && m_Engine.GetScene().Registry().valid(m_SelectedEntity)) {
        Entity sel(m_SelectedEntity, &m_Engine.GetScene().Registry());
        if (sel.Has<TransformComponent>()) {
            ImGuizmo::BeginFrame();
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(screenPos.x, screenPos.y, size.x, size.y);

            // While playing, the gizmo must follow the camera the scene is
            // actually rendered from (the play camera), not the fly camera.
            RenderCamera cam = BuildEditorCamera();
            if (m_Engine.State() != EngineRunState::Editing) {
                if (Entity primary = m_Engine.GetScene().PrimaryCamera()) {
                    auto& ptc = primary.Get<TransformComponent>();
                    auto& pcc = primary.Get<CameraComponent>();
                    cam.position = vec3(ptc.worldMatrix[3]);
                    cam.view = glm::inverse(ptc.worldMatrix);
                    cam.proj = MakeProjectionMatrix(pcc.fovDeg, size.x / size.y, pcc.nearClip, pcc.farClip);
                }
            }

            auto& tc = sel.Get<TransformComponent>();
            mat4 world = tc.worldMatrix;

            ImGuizmo::OPERATION op = m_GizmoOperation == 0 ? ImGuizmo::TRANSLATE
                                   : (m_GizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
            if (ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(cam.proj), op, ImGuizmo::WORLD,
                                     glm::value_ptr(world))) {
                // Read the manipulated transform back *after* the edit so the
                // gizmo result isn't undone by the transform we passed in.
                tc.local = Transform::FromMatrix(world);
                m_Engine.GetScene().UpdateTransforms();
                m_SoftwarePreviewDirty = true;
            }
        }
    }

    DrawViewportOverlay();

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
    if (ImGui::IsItemClicked()) {
        m_SelectedEntity = e;
        m_SoftwarePreviewDirty = true;
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Select")) m_SelectedEntity = e;
        if (ImGui::MenuItem("Frame (F)")) { m_SelectedEntity = e; m_RequestFrameSelection = true; }
        if (ImGui::MenuItem("Delete")) {
            m_Engine.GetScene().DestroyEntity(entity);
            m_SelectedEntity = entt::null;
            ImGui::EndPopup();
            return;
        }
        ImGui::EndPopup();
    }

    if (!hierarchy.children.empty() && opened) {
        for (auto child : hierarchy.children) DrawEntityNode(child);
        ImGui::TreePop();
    }
}

void EditorApp::DrawHierarchyPanel() {
    ImGui::Begin("Hierarchy");

    if (ImGui::Button("+ Create Entity")) {
        m_SelectedEntity = m_Engine.GetScene().CreateEntity("Entity").Handle();
        m_SoftwarePreviewDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && m_SelectedEntity != entt::null) {
        m_Engine.GetScene().DestroyEntity(Entity(m_SelectedEntity, &m_Engine.GetScene().Registry()));
        m_SelectedEntity = entt::null;
        m_SoftwarePreviewDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame (F)")) m_RequestFrameSelection = true;
    ImGui::Separator();

    if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        m_Engine.GetScene().Each<HierarchyComponent>([this](Entity e, HierarchyComponent& h) {
            if (h.parent == entt::null) DrawEntityNode(e.Handle());
        });
        ImGui::TreePop();
    }
    ImGui::End();
}

} // namespace fw
