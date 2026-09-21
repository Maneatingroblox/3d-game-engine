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

    // Switches between the Godot-style single-viewport layout and the Hammer
    // four-view brush-editing layout. Also driven by the toolbar's
    // "[ Hammer Mode ]" button; exposed here so tools/fwui can screenshot it.
    void SetEditorMode(EditorMode mode) { m_Mode = mode; }
    EditorMode GetEditorMode() const { return m_Mode; }

    // Resolution fraction used for the CPU viewport preview (1.0 = full size).
    // The default (0.5) keeps the interactive CPU path responsive; screenshot
    // tools ask for 1.0.
    void SetViewportPreviewScale(float scale) { m_SoftwarePreviewScale = scale; }

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
    // Script editor file I/O (both resolve through Paths::Resolve()).
    bool LoadScriptIntoEditor(const std::string& path);
    bool SaveOpenScript();
    void DrawHammerToolPanel();
    void DrawHammer2DViews();
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

    // ---- picking (editor/src/EditorPicking.cpp) ----------------------------
    // Ray through a viewport pixel, in world space.
    Ray ScreenPointToRay(const vec2& screenPixel, const vec2& viewportPos,
                         const vec2& viewportSize, const RenderCamera& cam) const;
    // Closest entity along a ray: AABB reject, then exact triangles.
    entt::entity PickEntityAt(const Ray& ray, float* outDistance) const;
    void HandleViewportPicking();
    // Selects the entity that owns a brush, so a click in a 2D pane also drives
    // the Inspector and the gizmo.
    void SelectEntityForBrush(UUID brushId);
    static void InvalidatePickCache();

    // Z-lock: Hammer's mouselook toggle for the 3D view. While engaged the
    // cursor is hidden and re-centred every frame, so the camera turns without
    // the pointer ever reaching a panel edge.
    void UpdateCameraLock();
    void SetCameraLock(bool locked);

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

    // Pan/zoom of each Hammer 2D pane (Top / Front / Side).
    struct HammerViewState {
        float panX = 0.0f;   // world units, along the pane's horizontal axis
        float panY = 0.0f;   // world units, along the pane's vertical axis
        float zoom = 16.0f;  // pixels per world unit
    };
    HammerViewState m_HammerViews[3];
    // Hammer-style grid snapping for brush creation/editing. The grid size is
    // shared by the 2D panes and the [ / ] hotkeys, so all four views and the
    // snapping agree on one value (as in Hammer).
    bool m_BrushSnapEnabled = true;
    float m_BrushGridSize = 1.0f;
    // Grid size is quantised to powers of two, the way Hammer steps it.
    static constexpr float kMinGridSize = 0.03125f;  // 1/32
    static constexpr float kMaxGridSize = 128.0f;
    void StepGridSize(int direction);   // -1 = '[' smaller, +1 = ']' bigger

    // Z-lock (mouselook) state for the 3D viewport.
    bool m_CameraLocked = false;
    vec2 m_LockAnchor{0.0f};       // screen point the cursor is re-centred to
    bool m_LockAnchorValid = false;

    // A click in the viewport is recorded during the ImGui pass and resolved in
    // the next OnUpdate(): picking needs the camera/viewport rect that the
    // frame was actually drawn with.
    bool m_PendingPick = false;
    vec2 m_PendingPickPos{0.0f};
    // Hammer mode gets its own dock layout (4 views + tool palette); this
    // tracks which layout is currently built so switching modes rebuilds it.
    EditorMode m_BuiltLayoutMode = EditorMode::Scene;

    // Edit-time fly camera state
    vec3 m_CamPos{0, 3, 8};
    float m_CamYaw = -90.0f, m_CamPitch = -15.0f;
    float m_CamSpeed = 6.0f;
    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
    // True while a right-mouse "fly" drag owns the mouse: the look keeps
    // working even when the cursor wanders off the viewport panel.
    bool m_CameraCaptured = false;
    vec2 m_ViewportSize{1280, 720};
    vec2 m_ViewportPos{0, 0};

    int m_GizmoOperation = 0; // ImGuizmo::TRANSLATE etc, stored as int to avoid pulling the header everywhere
    std::string m_CurrentScenePath = "assets/scenes/default.fwscene";
    std::string m_StartupScenePath;
    std::string m_OpenScriptPath;
    std::string m_ScriptEditBuffer;
    // Script editor UI state: last action message and unsaved-changes marker.
    std::string m_ScriptStatus;
    bool m_ScriptDirty = false;

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
