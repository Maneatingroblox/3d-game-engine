// Shared structures/constant buffers for the Forgeworks forward renderer.
#ifndef FORGEWORKS_COMMON_HLSLI
#define FORGEWORKS_COMMON_HLSLI

#define MAX_LIGHTS 32

struct GPULight {
    float3 positionOrDir; // world-space position (point/spot) or direction (directional)
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
    float4x4 gInvViewProj;      // for reconstructing world-space view rays (skybox, SSAO, etc.)
    float3   gCameraPos;
    float    gTime;
    float3   gAmbientColor;
    float    gAmbientIntensity;
    int      gLightCount;
    float3   _padFrame;
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

#endif
