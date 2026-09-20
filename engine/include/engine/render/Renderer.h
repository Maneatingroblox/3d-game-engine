#pragma once
// Forward renderer: shadow pass (primary directional/spot light) -> opaque
// pass (dynamic lights + baked lightmaps blended in-shader) -> skybox ->
// transparent pass -> (editor only) debug/gizmo overlays. Also renders the
// Dear ImGui draw data on top when running inside the editor.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include "engine/render/ShaderLibrary.h"
#include "engine/render/MaterialSystem.h"
#include "engine/asset/GpuMeshCache.h"
#include "engine/asset/TextureLoader.h"
#include <vector>

namespace fw {

class Scene;
class RenderDevice;
class Window;

struct RenderCamera {
    vec3 position{0.0f};
    mat4 view{1.0f};
    mat4 proj{1.0f};
};

struct RenderSettings {
    vec3 ambientColor{0.15f, 0.17f, 0.2f};
    float ambientIntensity = 1.0f;
    bool enableShadows = true;
    int shadowMapResolution = 2048;
    bool wireframe = false;
    bool drawGrid = true;
    bool drawColliders = false;
};

class Renderer {
public:
    explicit Renderer(RenderDevice* device);
    ~Renderer();

    bool Init();
    void Shutdown();

    // Renders the whole scene from `camera`. If targetRTV/targetDSV are null,
    // renders into the swapchain's back buffer (the game's main window);
    // otherwise renders into the given target (the editor's viewport panel
    // renders into an off-screen texture that ImGui then displays as an
    // image, exactly like Godot's viewport-as-texture approach).
#if FW_PLATFORM_WINDOWS
    void RenderScene(Scene& scene, const RenderCamera& camera, const RenderSettings& settings,
                      ID3D11RenderTargetView* targetRTV = nullptr, ID3D11DepthStencilView* targetDSV = nullptr,
                      int targetWidth = 0, int targetHeight = 0);
#endif

    void OnResize(int width, int height);

    GpuMeshCache& MeshCache() { return m_MeshCache; }
    TextureLoader& Textures() { return m_TextureLoader; }
    MaterialSystem& Materials() { return m_MaterialSystem; }
    ShaderLibrary& Shaders() { return m_ShaderLibrary; }

private:
    void RenderShadowPass(Scene& scene, const mat4& lightViewProj);
    void RenderOpaquePass(Scene& scene, const RenderCamera& camera, const mat4& lightViewProj, const RenderSettings& settings);
    void RenderSkybox(Scene& scene, const RenderCamera& camera);
    mat4 ComputePrimaryLightViewProj(Scene& scene, const RenderCamera& camera);

    RenderDevice* m_Device;
    GpuMeshCache m_MeshCache;
    TextureLoader m_TextureLoader;
    MaterialSystem m_MaterialSystem;
    ShaderLibrary m_ShaderLibrary;

#if FW_PLATFORM_WINDOWS
    ComPtr<ID3D11Buffer> m_FrameCB;
    ComPtr<ID3D11Buffer> m_ObjectCB;
    ComPtr<ID3D11Buffer> m_LightCB;
    ComPtr<ID3D11Buffer> m_MaterialCB;
    ComPtr<ID3D11SamplerState> m_LinearSampler;
    ComPtr<ID3D11SamplerState> m_ShadowSampler;
    ComPtr<ID3D11RasterizerState> m_DefaultRasterizer;
    ComPtr<ID3D11RasterizerState> m_WireframeRasterizer;
    ComPtr<ID3D11RasterizerState> m_ShadowRasterizer;
    ComPtr<ID3D11DepthStencilState> m_DefaultDepthState;
    ComPtr<ID3D11BlendState> m_OpaqueBlendState;
    ComPtr<ID3D11BlendState> m_TransparentBlendState;

    // Shadow map resources
    ComPtr<ID3D11Texture2D> m_ShadowMapTexture;
    ComPtr<ID3D11DepthStencilView> m_ShadowMapDSV;
    ComPtr<ID3D11ShaderResourceView> m_ShadowMapSRV;
    int m_ShadowMapSize = 2048;
#endif
};

} // namespace fw
