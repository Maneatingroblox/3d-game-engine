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
protected:
    bool OnInit() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnImGui() override;
    void OnShutdown() override;
    void OnResize(int width, int height) override;

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
    std::string m_OpenScriptPath;
    std::string m_ScriptEditBuffer;

    bool m_ShowDemoWindow = false;
    float m_BakeProgress = -1.0f;
    std::string m_BakeStatus;

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
