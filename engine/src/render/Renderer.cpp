#include "engine/render/Renderer.h"
#include "engine/render/RenderDevice.h"
#include "engine/scene/Scene.h"
#include "engine/core/Log.h"

#if FW_PLATFORM_WINDOWS

namespace fw {

struct GPULightGPU {
    vec3 positionOrDir; float type;
    vec3 color; float intensity;
    vec3 spotDirection; float range;
    float innerCos, outerCos, castsShadow, pad0;
};

struct FrameConstantsGPU {
    mat4 viewProj;
    mat4 view;
    mat4 lightViewProj;
    mat4 invViewProj;
    vec3 cameraPos; float time;
    vec3 ambientColor; float ambientIntensity;
    int lightCount;
    float gridCellSize;
    vec2 pad;
};

struct ObjectConstantsGPU {
    mat4 world;
    mat4 worldInvTranspose;
    int useLightmap;
    int receiveShadows;
    vec2 pad;
};

struct LightConstantsGPU {
    GPULightGPU lights[32];
};

Renderer::Renderer(RenderDevice* device)
    : m_Device(device), m_MeshCache(device), m_TextureLoader(device),
      m_MaterialSystem(device, &m_TextureLoader), m_ShaderLibrary(device) {}

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init() {
    ID3D11Device* dev = m_Device->Device();

    auto makeCB = [&](UINT size, ComPtr<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.ByteWidth = (size + 15) & ~15u;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev->CreateBuffer(&desc, nullptr, &out);
    };
    makeCB(sizeof(FrameConstantsGPU), m_FrameCB);
    makeCB(sizeof(ObjectConstantsGPU), m_ObjectCB);
    makeCB(sizeof(LightConstantsGPU), m_LightCB);
    makeCB(256, m_MaterialCB); // sized generously; MaterialSystem writes its own struct layout

    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.MaxAnisotropy = 8;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sampDesc, &m_LinearSampler);

    D3D11_SAMPLER_DESC shadowSampDesc{};
    shadowSampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampDesc.AddressU = shadowSampDesc.AddressV = shadowSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowSampDesc.BorderColor[0] = shadowSampDesc.BorderColor[1] = shadowSampDesc.BorderColor[2] = shadowSampDesc.BorderColor[3] = 1.0f;
    shadowSampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateSamplerState(&shadowSampDesc, &m_ShadowSampler);

    // WINDING CONVENTION - this must match the geometry the engine produces.
    //
    // Every mesh in Forgeworks (MeshData::CreateBox/Sphere/Cylinder/..., the
    // brush compiler, RecalculateNormals()) is authored counter-clockwise as
    // seen from OUTSIDE the surface, and the CPU reference renderer culls on
    // that basis. In D3D11's screen space Y points down, so such a front face
    // appears *counter-clockwise* and must be declared as such.
    //
    // FrontCounterClockwise defaults to FALSE, which told the rasterizer the
    // exact opposite: every outward-facing triangle was treated as a back face
    // and culled, leaving only the far side of each object on screen. That is
    // the "shapes look like their vertices are inverted" symptom - you were
    // seeing each object's interior/back wall instead of its front.
    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = TRUE;
    rsDesc.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rsDesc, &m_DefaultRasterizer);
    rsDesc.FillMode = D3D11_FILL_WIREFRAME;
    rsDesc.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rsDesc, &m_WireframeRasterizer);

    D3D11_RASTERIZER_DESC shadowRs{};
    shadowRs.FillMode = D3D11_FILL_SOLID;
    shadowRs.CullMode = D3D11_CULL_FRONT; // reduce peter-panning
    shadowRs.FrontCounterClockwise = TRUE; // same convention as the main pass
    shadowRs.DepthClipEnable = TRUE;
    shadowRs.DepthBias = 5000;
    shadowRs.DepthBiasClamp = 0.0f;
    shadowRs.SlopeScaledDepthBias = 2.0f;
    dev->CreateRasterizerState(&shadowRs, &m_ShadowRasterizer);

    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS;
    dev->CreateDepthStencilState(&dsDesc, &m_DefaultDepthState);

    // Skybox + ground grid: depth-tested so geometry hides them, but they never
    // write depth (they must not occlude anything drawn later).
    D3D11_DEPTH_STENCIL_DESC noWriteDesc{};
    noWriteDesc.DepthEnable = TRUE;
    noWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    noWriteDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateDepthStencilState(&noWriteDesc, &m_SkyDepthState);
    dev->CreateDepthStencilState(&noWriteDesc, &m_GridDepthState);

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&blendDesc, &m_OpaqueBlendState);

    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dev->CreateBlendState(&blendDesc, &m_TransparentBlendState);

    // Shadow map
    m_ShadowMapSize = 2048;
    D3D11_TEXTURE2D_DESC shadowDesc{};
    shadowDesc.Width = shadowDesc.Height = m_ShadowMapSize;
    shadowDesc.MipLevels = 1;
    shadowDesc.ArraySize = 1;
    shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.Usage = D3D11_USAGE_DEFAULT;
    shadowDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&shadowDesc, nullptr, &m_ShadowMapTexture);

    D3D11_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc{};
    shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    shadowDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(m_ShadowMapTexture.Get(), &shadowDsvDesc, &m_ShadowMapDSV);

    D3D11_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc{};
    shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shadowSrvDesc.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(m_ShadowMapTexture.Get(), &shadowSrvDesc, &m_ShadowMapSRV);

    FW_LOG_INFO("Renderer initialized");
    return true;
}

void Renderer::Shutdown() {}

void Renderer::OnResize(int width, int height) { FW_UNUSED(width); FW_UNUSED(height); }

bool Renderer::MeshShaderAvailable() {
    return m_ShaderLibrary.HasFailed("assets/shaders/Mesh.hlsl") ? false
        : m_ShaderLibrary.LoadMeshShader("assets/shaders/Mesh.hlsl") != nullptr;
}

mat4 Renderer::ComputePrimaryLightViewProj(Scene& scene, const RenderCamera& camera) {
    // The shadow-casting light travels along its forward axis; the light basis
    // below maps that onto the direction light travels (same convention as the
    // GPU light array: `positionOrDir` is a *direction* for directional lights
    // and shading uses L = -direction).
    vec3 lightDir(0.3f, -1.0f, 0.2f);
    bool found = false;
    scene.Each<TransformComponent, LightComponent>([&](Entity, TransformComponent& tc, LightComponent& lc) {
        if (found || !lc.castsShadows || lc.type != LightType::Directional) return;
        lightDir = tc.local.Forward();
        found = true;
    });
    if (glm::length2(lightDir) < 1e-8f) lightDir = vec3(0.3f, -1.0f, 0.2f);
    lightDir = glm::normalize(lightDir);

    // Fit the ortho box around the camera frustum so the shadow map is used
    // efficiently and follows the view instead of being anchored at the world
    // origin (which made shadows vanish as soon as the camera moved away).
    const float depth = 45.0f;
    const float halfHeight = depth * std::tan(Radians(30.0f));  // ~60 deg vertical fov
    const float halfWidth = halfHeight * 1.9f;                  // generous: covers wide aspects
    const vec3 focus = camera.position + camera.Forward() * depth;
    const float radius = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight + depth * depth * 0.25f);

    const vec3 eye = focus - lightDir * (radius * 2.0f);
    const mat4 lightView = glm::lookAt(eye, focus, std::abs(lightDir.y) > 0.99f ? vec3(0, 0, 1) : vec3(0, 1, 0));
    const mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
    return lightProj * lightView;
}

void Renderer::RenderShadowPass(Scene& scene, const mat4& lightViewProj) {
    ID3D11DeviceContext* ctx = m_Device->Context();
    ctx->OMSetRenderTargets(0, nullptr, m_ShadowMapDSV.Get());
    ctx->ClearDepthStencilView(m_ShadowMapDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp{ 0, 0, (float)m_ShadowMapSize, (float)m_ShadowMapSize, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(m_ShadowRasterizer.Get());
    ctx->OMSetDepthStencilState(m_DefaultDepthState.Get(), 0);

    ShaderProgram* shader = m_ShaderLibrary.LoadDepthOnlyShader("assets/shaders/ShadowDepth.hlsl");
    if (!shader) return;
    ctx->IASetInputLayout(shader->inputLayout.Get());
    ctx->VSSetShader(shader->vs.Get(), nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    scene.Each<TransformComponent, MeshRendererComponent>([&](Entity, TransformComponent& tc, MeshRendererComponent& mr) {
        if (!mr.castShadows || mr.meshAsset.empty()) return;
        GpuMesh* mesh = m_MeshCache.Load(mr.meshAsset);
        if (!mesh) return;

        ObjectConstantsGPU obj{};
        obj.world = tc.worldMatrix;
        obj.worldInvTranspose = glm::transpose(glm::inverse(tc.worldMatrix));
        ctx->UpdateSubresource(m_ObjectCB.Get(), 0, nullptr, &obj, 0, 0);
        ctx->VSSetConstantBuffers(1, 1, m_ObjectCB.GetAddressOf());

        UINT stride = sizeof(Vertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, mesh->vertexBuffer.GetAddressOf(), &stride, &offset);
        ctx->IASetIndexBuffer(mesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->DrawIndexed(mesh->indexCount, 0, 0);
    });
}

void Renderer::RenderOpaquePass(Scene& scene, const RenderCamera& camera, const mat4& lightViewProj, const RenderSettings& settings) {
    ID3D11DeviceContext* ctx = m_Device->Context();

    FrameConstantsGPU frame{};
    frame.viewProj = camera.proj * camera.view;
    frame.view = camera.view;
    frame.lightViewProj = lightViewProj;
    frame.invViewProj = glm::inverse(frame.viewProj);
    frame.cameraPos = camera.position;
    frame.time = 0.0f;
    frame.ambientColor = settings.ambientColor;
    frame.ambientIntensity = settings.ambientIntensity;
    frame.gridCellSize = settings.drawGrid ? std::max(settings.gridCellSize, 0.0f) : 0.0f;

    LightConstantsGPU lightsData{};
    int lightCount = 0;
    scene.Each<TransformComponent, LightComponent>([&](Entity, TransformComponent& tc, LightComponent& lc) {
        if (lightCount >= 32) return;
        auto& gl = lightsData.lights[lightCount];
        gl.type = lc.type == LightType::Directional ? 0.0f : (lc.type == LightType::Point ? 1.0f : 2.0f);
        gl.positionOrDir = lc.type == LightType::Directional ? tc.local.Forward() : tc.local.position;
        gl.color = lc.color;
        gl.intensity = lc.intensity;
        gl.spotDirection = tc.local.Forward();
        gl.range = lc.range;
        gl.innerCos = std::cos(Radians(lc.innerConeDeg));
        gl.outerCos = std::cos(Radians(lc.outerConeDeg));
        gl.castsShadow = (lc.castsShadows && settings.enableShadows) ? 1.0f : 0.0f;
        lightCount++;
    });
    frame.lightCount = lightCount;

    ctx->UpdateSubresource(m_FrameCB.Get(), 0, nullptr, &frame, 0, 0);
    ctx->UpdateSubresource(m_LightCB.Get(), 0, nullptr, &lightsData, 0, 0);
    ctx->VSSetConstantBuffers(0, 1, m_FrameCB.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, m_FrameCB.GetAddressOf());
    ctx->PSSetConstantBuffers(2, 1, m_LightCB.GetAddressOf());
    ctx->PSSetSamplers(0, 1, m_LinearSampler.GetAddressOf());
    ctx->PSSetSamplers(1, 1, m_ShadowSampler.GetAddressOf());
    ctx->PSSetShaderResources(5, 1, m_ShadowMapSRV.GetAddressOf());

    ctx->RSSetState(settings.wireframe ? m_WireframeRasterizer.Get() : m_DefaultRasterizer.Get());
    ctx->OMSetDepthStencilState(m_DefaultDepthState.Get(), 0);
    ctx->OMSetBlendState(m_OpaqueBlendState.Get(), nullptr, 0xFFFFFFFF);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ShaderProgram* shader = m_ShaderLibrary.LoadMeshShader("assets/shaders/Mesh.hlsl");
    if (!shader) return;
    ctx->IASetInputLayout(shader->inputLayout.Get());
    ctx->VSSetShader(shader->vs.Get(), nullptr, 0);
    ctx->PSSetShader(shader->ps.Get(), nullptr, 0);

    scene.Each<TransformComponent, MeshRendererComponent>([&](Entity, TransformComponent& tc, MeshRendererComponent& mr) {
        if (mr.meshAsset.empty()) return;
        GpuMesh* mesh = m_MeshCache.Load(mr.meshAsset);
        if (!mesh) return;

        ObjectConstantsGPU obj{};
        obj.world = tc.worldMatrix;
        obj.worldInvTranspose = glm::transpose(glm::inverse(tc.worldMatrix));
        obj.useLightmap = mr.useLightmap ? 1 : 0;
        obj.receiveShadows = mr.receiveShadows ? 1 : 0;
        ctx->UpdateSubresource(m_ObjectCB.Get(), 0, nullptr, &obj, 0, 0);
        ctx->VSSetConstantBuffers(1, 1, m_ObjectCB.GetAddressOf());
        ctx->PSSetConstantBuffers(1, 1, m_ObjectCB.GetAddressOf());

        if (mr.useLightmap && !mr.lightmapAsset.empty()) {
            GpuTexture* lm = m_TextureLoader.Load(mr.lightmapAsset);
            ID3D11ShaderResourceView* srv = (lm ? lm : m_TextureLoader.GetWhiteTexture())->srv.Get();
            ctx->PSSetShaderResources(4, 1, &srv);
        }

        UINT stride = sizeof(Vertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, mesh->vertexBuffer.GetAddressOf(), &stride, &offset);
        ctx->IASetIndexBuffer(mesh->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

        for (auto& sub : mesh->subMeshes) {
            Material* mat = m_MaterialSystem.GetDefault();
            if (sub.materialIndex >= 0 && sub.materialIndex < (int)mr.materialSlots.size() && !mr.materialSlots[sub.materialIndex].empty()) {
                mat = m_MaterialSystem.Load(mr.materialSlots[sub.materialIndex]);
            }
            m_MaterialSystem.Bind(ctx, m_MaterialCB.Get(), *mat);
            ctx->DrawIndexed(sub.indexCount, sub.indexStart, 0);
        }
    });
}

void Renderer::RenderSkybox(Scene& scene, const RenderCamera& camera) {
    ID3D11DeviceContext* ctx = m_Device->Context();
    ShaderProgram* shader = m_ShaderLibrary.LoadFullscreenShader("assets/shaders/Skybox.hlsl");
    if (!shader) return;

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(shader->vs.Get(), nullptr, 0);
    ctx->PSSetShader(shader->ps.Get(), nullptr, 0);

    ctx->OMSetDepthStencilState(m_SkyDepthState.Get(), 0);

    ctx->Draw(3, 0);

    ctx->OMSetDepthStencilState(m_DefaultDepthState.Get(), 0);
    FW_UNUSED(scene); FW_UNUSED(camera);
}

// Ground grid: fullscreen pass drawn after the scene, depth-tested but not
// depth-writing (see assets/shaders/Grid.hlsl). This is what keeps the
// viewport readable when a scene contains no geometry yet.
void Renderer::RenderGrid(const RenderSettings& settings, int width, int height) {
    if (!settings.drawGrid || settings.gridCellSize <= 0.0f) return;

    ID3D11DeviceContext* ctx = m_Device->Context();
    ShaderProgram* shader = m_ShaderLibrary.LoadFullscreenShader("assets/shaders/Grid.hlsl");
    if (!shader) return;

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(shader->vs.Get(), nullptr, 0);
    ctx->PSSetShader(shader->ps.Get(), nullptr, 0);

    D3D11_VIEWPORT vp{ 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(m_DefaultRasterizer.Get());

    // Depth test on (so geometry occludes the grid), depth writes off (the
    // grid never occludes anything drawn later).
    ctx->OMSetDepthStencilState(m_GridDepthState.Get(), 0);

    ctx->OMSetBlendState(m_TransparentBlendState.Get(), nullptr, 0xFFFFFFFF);
    ctx->Draw(3, 0);

    ctx->OMSetDepthStencilState(m_DefaultDepthState.Get(), 0);
    ctx->OMSetBlendState(m_OpaqueBlendState.Get(), nullptr, 0xFFFFFFFF);
}

void Renderer::RenderScene(Scene& scene, const RenderCamera& camera, const RenderSettings& settings,
                            ID3D11RenderTargetView* targetRTV, ID3D11DepthStencilView* targetDSV,
                            int targetWidth, int targetHeight) {
    mat4 lightViewProj = ComputePrimaryLightViewProj(scene, camera);

    if (settings.enableShadows) RenderShadowPass(scene, lightViewProj);

    ID3D11DeviceContext* ctx = m_Device->Context();
    ID3D11RenderTargetView* rtv = targetRTV ? targetRTV : m_Device->BackBufferRTV();
    ID3D11DepthStencilView* dsv = targetDSV ? targetDSV : m_Device->DepthStencilView();
    int width = targetRTV ? targetWidth : m_Device->Width();
    int height = targetRTV ? targetHeight : m_Device->Height();

    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    if (targetRTV) {
        const float clear[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
        ctx->ClearRenderTargetView(targetRTV, clear);
    }
    D3D11_VIEWPORT vp{ 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);

    RenderSkybox(scene, camera);
    RenderOpaquePass(scene, camera, lightViewProj, settings);
    // Grid last: visible over the sky/background, hidden behind geometry.
    RenderGrid(settings, width, height);
}

} // namespace fw

#endif // FW_PLATFORM_WINDOWS
