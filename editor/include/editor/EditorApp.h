#pragma once
// The Map Maker: a Godot-style editor (3D viewport, scene hierarchy,
// inspector, asset browser, script editor, play/stop) with a toggleable
// "Hammer mode" for brush-based level geometry authoring. Both modes edit
// the same underlying Engine/Scene, so scripts and gameplay behave
// identically whether geometry came from meshes or brushes.

#include "engine/core/Application.h"
#include "engine/core/Engine.h"
#include "engine/core/UUID.h"
#include "engine/asset/AssetDatabase.h"
#include "engine/render/RenderTypes.h"
#include "engine/render/SoftwareRenderer.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>

#if FW_PLATFORM_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace fw {

enum class EditorMode { Scene, Hammer };
enum class BrushTool { Select, Block, Clip, Vertex, Carve };

class EditorApp : public Application {
public:
    // Scene the editor opens on startup (empty = assets/scenes/default.fwscene).
    void SetStartupScene(const std::string& path) { m_StartupScenePath = path; }

protected:
    bool OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnImGui() override;
    void OnShutdown() override;
    void OnResize(int width, int height) override;
    // Called by Application::RunWithScreenshot() after the window image is
    // written: also drops a viewport-only PNG next to it.
    void OnScreenshot(const std::string& path) override;

private:
    // Panels
    void DrawMenuBar();
    void DrawToolbar();
    void DrawViewportPanel();
    void DrawHierarchyPanel();
    void DrawInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawConsolePanel();
    void DrawScriptEditorPanel();
    void DrawHammerToolPanel();
    void DrawLightmapBakePanel();
    void DrawEntityNode(entt::entity e);

    // Viewport camera (edit-time fly camera; separate from any in-scene CameraComponent)
    void UpdateEditorCamera(float dt);
    RenderCamera BuildEditorCamera() const;
    void FrameSelection();          // F: move the camera so the selection is on screen
    void ResetEditorCamera();

    // Default dock layout (built once, on the first frame).
    // `dockspaceId` is an ImGuiID (unsigned int) - kept as a plain integer so
    // this header doesn't have to include imgui.h.
    void BuildDefaultLayout(unsigned int dockspaceId);
    void DrawViewportOverlay();

    // CPU-rendered preview of the viewport (engine/render/SoftwareRenderer.h).
    // Used as a always-available fallback so the viewport shows the scene even
    // when the GPU path can't produce an image, and by the screenshot tool.
    void UpdateSoftwarePreview(int width, int height);
    void DrawSoftwarePreviewImage(float width, float height);
    void CaptureViewportScreenshot(const std::string& path);
    bool SaveViewportImage(const std::string& path, const SoftwareImage& image);

    // Hammer-mode brush editing
    void UpdateHammerMode(float dt);
    void HandleBrushPicking();
    void ApplyClipTool();
    void ApplyCarveTool();

    void NewScene();
    void OpenScene(const std::string& path);
    void SaveScene();
    void SaveSceneAs(const std::string& path);

    Engine m_Engine;
    AssetDatabase m_AssetDatabase;
    EditorMode m_Mode = EditorMode::Scene;
    BrushTool m_BrushTool = BrushTool::Select;

    entt::entity m_SelectedEntity = entt::null;
    UUID m_SelectedBrush{0};

    // Edit-time fly camera state
    vec3 m_CamPos{0, 3, 8};
    float m_CamYaw = -90.0f, m_CamPitch = -15.0f;
    float m_CamSpeed = 6.0f;
    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
    vec2 m_ViewportSize{1280, 720};
    vec2 m_ViewportPos{0, 0};

    int m_GizmoOperation = 0; // ImGuizmo::TRANSLATE etc, stored as int to avoid pulling the header everywhere
    std::string m_CurrentScenePath = "assets/scenes/default.fwscene";
    std::string m_StartupScenePath;
    std::string m_OpenScriptPath;
    std::string m_ScriptEditBuffer;

    bool m_ShowDemoWindow = false;
    float m_BakeProgress = -1.0f;
    std::string m_BakeStatus;

    // Viewport / rendering options (previously hard-coded, so there was no way
    // to diagnose a black viewport from inside the editor).
    RenderSettings m_RenderSettings;
    bool m_SoftwarePreview = false;   // render the viewport on the CPU instead of D3D11
    bool m_SoftwarePreviewDirty = true;
    float m_SoftwarePreviewScale = 0.5f; // fraction of the viewport resolution
    SoftwareImage m_SoftwareImage;
    bool m_ShowViewportOverlay = true;
    bool m_DefaultLayoutBuilt = false;
    // One-shot check a few frames after startup that the Viewport really is
    // docked and visible: a stale imgui.ini written by an older build can leave
    // it hidden as a tab behind other panels, which looks exactly like a broken
    // editor. When the check fails the default layout is rebuilt.
    int m_LayoutCheckCountdown = 4;
    bool m_RequestFrameSelection = false;
    float m_LastSoftwareMs = 0.0f;

#if FW_PLATFORM_WINDOWS
    void CreateSoftwarePreviewTexture(const SoftwareImage& image);
    void ReleaseSoftwarePreviewTexture();
    ComPtr<ID3D11Texture2D> m_PreviewTex;
    ComPtr<ID3D11ShaderResourceView> m_PreviewSRV;
    int m_PreviewTexWidth = 0, m_PreviewTexHeight = 0;
#endif

#if FW_PLATFORM_WINDOWS
    void CreateViewportTarget(int width, int height);
    ComPtr<ID3D11Texture2D> m_ViewportColorTex;
    ComPtr<ID3D11RenderTargetView> m_ViewportRTV;
    ComPtr<ID3D11ShaderResourceView> m_ViewportSRV;
    ComPtr<ID3D11Texture2D> m_ViewportDepthTex;
    ComPtr<ID3D11DepthStencilView> m_ViewportDSV;
    int m_ViewportTexWidth = 0, m_ViewportTexHeight = 0;
#endif
};

} // namespace fw
