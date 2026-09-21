#pragma once
// The Map Maker: a Godot-style editor (3D viewport, scene hierarchy,
// inspector, asset browser, script editor, play/stop) with a toggleable
// "Hammer mode" that switches the whole GUI to a Valve Hammer-style layout -
// four synchronized viewports (3D camera + top/front/side orthographic views)
// and brush tools (Block/Clip/Carve/Vertex). Both modes edit the same
// underlying Engine/Scene, so scripts and gameplay behave identically whether
// geometry came from meshes or brushes.

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
enum class EditorLayout { None, Scene, Hammer };

class EditorApp : public Application {
public:
    // Scene the editor opens on startup (empty = assets/scenes/default.fwscene).
    void SetStartupScene(const std::string& path) { m_StartupScenePath = path; }

    // Resolution fraction used for the CPU viewport preview (1.0 = full size).
    // The default (0.5) keeps the interactive CPU path responsive; screenshot
    // tools ask for 1.0.
    void SetViewportPreviewScale(float scale) { m_SoftwarePreviewScale = scale; }

    // Scene mode = Godot-style single viewport; Hammer mode = the quad-view
    // brush editor. Used by tools/fwui to capture both GUIs.
    void SetEditorMode(EditorMode mode) { m_Mode = mode; m_ActiveLayout = EditorLayout::None; }
    EditorMode GetEditorMode() const { return m_Mode; }

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
    // The app moved to the CPU presentation path: there is no GPU viewport
    // target any more, so use the CPU viewport preview and drop the targets that
    // belonged to the destroyed D3D11 device.
    void OnSoftwarePresentation() override;

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

    // Hammer-mode orthographic views (top/front/side). `view`: 0 = top (X/Z),
    // 1 = front (X/Y), 2 = side (Z/Y). Drawn as 2D wireframe over a grid,
    // Hammer-style: pan with MMB drag, zoom with the wheel, LMB drags out
    // blocks (Block tool) or picks brushes (Select tool).
    void DrawOrthoViewPanel(const char* title, int view);
    struct OrthoView {
        vec2 center{0.0f, 0.0f}; // world-space in-plane centre (u,w coords)
        float zoom = 40.0f;      // screen pixels per world unit
    };
    // Maps a world position into an ortho view's (u, w) plane coordinates.
    static void OrthoViewAxes(int view, vec3& right, vec3& up);
    static void OrthoViewPlaneCoords(int view, const vec3& world, float& u, float& w);
    void OrthoViewWorldPoint(int view, const vec2& mouseScreenPos, const ImVec2& rectPos,
                             const ImVec2& rectSize, vec3& outWorld) const;
    void DrawOrthoViewContent(int view, struct ImDrawList* dl, const ImVec2& pos, const ImVec2& size);
    void HandleOrthoViewInput(int view, const ImVec2& rectPos, const ImVec2& rectSize);

    // Viewport camera (edit-time fly camera; separate from any in-scene CameraComponent)
    void UpdateEditorCamera(float dt);
    RenderCamera BuildEditorCamera() const;
    void FrameSelection();          // F: move the camera so the selection is on screen
    void ResetEditorCamera();

    // Default dock layouts (built once per mode switch).
    // `dockspaceId` is an ImGuiID (unsigned int) - kept as a plain integer so
    // this header doesn't have to include imgui.h.
    void BuildDefaultLayout(unsigned int dockspaceId);
    void BuildHammerLayout(unsigned int dockspaceId);
    void EnsureLayout(unsigned int dockspaceId);
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
    void CreateBlockBrush(const vec3& mins, const vec3& maxs);

    // Script editor
    void OpenScript(const std::string& path);   // loads into the editor buffer
    bool SaveOpenScript();                      // writes the buffer to disk
    void NewScriptFile();                       // creates + opens a fresh .lua

    void NewScene();
    void OpenScene(const std::string& path);
    void SaveScene();
    void SaveSceneAs(const std::string& path);

    Engine m_Engine;
    AssetDatabase m_AssetDatabase;
    EditorMode m_Mode = EditorMode::Scene;
    EditorLayout m_ActiveLayout = EditorLayout::None;
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
    std::string m_ScriptStatus;
    bool m_ScriptDirty = false;

    // Hammer-mode 2D views + block tool
    OrthoView m_OrthoViews[3];
    bool m_BlockDragging = false;
    int m_BlockDragView = -1;
    float m_BlockDragU0 = 0.0f, m_BlockDragW0 = 0.0f; // drag start, in-plane
    float m_BlockDragU1 = 0.0f, m_BlockDragW1 = 0.0f; // current end, in-plane
    float m_BlockHeight = 2.0f;   // top-view extrusion (Y)
    float m_BlockBaseY = 0.0f;
    float m_BlockDepth = 4.0f;    // front/side-view extrusion (Z / X)
    float m_GridSnap = 1.0f;      // 0 = off

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
    // Id of the CPU viewport image inside SoftCanvas (Application::Canvas())
    // when the app runs on the software presentation path. 0 = not uploaded yet.
    unsigned int m_SoftPreviewTexId = 0;
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
