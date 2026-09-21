// Shared structures/constant buffers for the Forgeworks forward renderer.
#ifndef FORGEWORKS_COMMON_HLSLI
#define FORGEWORKS_COMMON_HLSLI

// ---------------------------------------------------------------------------
// MATRIX CONVENTION (keep in sync with engine/include/engine/render/RenderTypes.h)
//
// Forgeworks uploads glm matrices (column-major storage) straight into these
// constant buffers. HLSL's default matrix packing is column-major as well, so
// a cbuffer float4x4 holds exactly the glm matrix - which means every
// transform must be written as:
//
//     float4 clip = mul(gWorldViewProj, float4(position, 1.0));
//
// Writing mul(vector, matrix) instead multiplies by the *transpose*, which
// for a perspective matrix throws every vertex outside the clip volume (an
// entirely blank viewport) and for a rotation matrix mirrors it.
// ---------------------------------------------------------------------------

#define MAX_LIGHTS 32

struct GPULight {
    float3 positionOrDir; // world-space position (point/spot) or forward direction (directional)
    float  type;          // 0 = directional, 1 = point, 2 = spot
    float3 color;
    float  intensity;
    float3 spotDirection;
    float  range;
    float  innerCos;
    float  outerCos;
    float  castsShadow;
    float  _pad0;
};

cbuffer FrameConstants : register(b0) {
    float4x4 gViewProj;
    float4x4 gView;
    float4x4 gLightViewProj;    // primary shadow-casting light's view-proj
    float4x4 gInvViewProj;      // for reconstructing world-space view rays (skybox, grid)
    float3   gCameraPos;
    float    gTime;
    float3   gAmbientColor;
    float    gAmbientIntensity;
    int      gLightCount;
    float    gGridCellSize;     // world units per ground-grid cell (0 = no grid)
    float2   _padFrame;
};

cbuffer ObjectConstants : register(b1) {
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    int      gUseLightmap;
    int      gReceiveShadows;
    float2   _padObject;
};

cbuffer LightConstants : register(b2) {
    GPULight gLights[MAX_LIGHTS];
};

cbuffer MaterialConstants : register(b3) {
    float4 gAlbedoColor;
    float3 gEmissiveColor;
    float  gMetallic;
    float  gRoughness;
    float  gEmissiveStrength;
    int    gHasAlbedoMap;
    int    gHasNormalMap;
    int    gHasMetallicRoughnessMap;
    int    gHasEmissiveMap;
    float3 _padMaterial;
};

SamplerState gLinearSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

Texture2D gAlbedoMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gMetallicRoughnessMap : register(t2);
Texture2D gEmissiveMap : register(t3);
Texture2D gLightmap : register(t4);
Texture2D gShadowMap : register(t5);

// Output transform: the swap chain / viewport render target is a plain UNORM
// format, so the renderer must produce display-referred colour itself.
// A Reinhard tonemap keeps bright lights (sun 3x + point lights) from clipping
// to a flat white blob, then gamma is applied. Mirrored by the CPU renderer in
// engine/src/render/SoftwareRenderer.cpp.
float3 FWToDisplay(float3 linearColor) {
    float3 mapped = linearColor / (linearColor + 1.0);
    return pow(saturate(mapped * 1.12), 1.0 / 2.2);
}

#endif
