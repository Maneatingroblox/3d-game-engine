// Procedural gradient sky (used when no skybox cubemap asset is assigned).
// Rendered as a fullscreen triangle, sampled by reconstructing the world-
// space view direction from clip-space position via the inverse view-proj.
#include "Common.hlsli"

struct VSOutput {
    float4 clipPos : SV_POSITION;
    float2 ndc     : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput o;
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
    o.clipPos = float4(pos, 0.9999, 1.0);
    o.ndc = pos;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    float4 nearPoint = mul(float4(input.ndc, 0.0, 1.0), gInvViewProj);
    float4 farPoint  = mul(float4(input.ndc, 1.0, 1.0), gInvViewProj);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    float3 dir = normalize(farPoint.xyz - nearPoint.xyz);

    float t = saturate(dir.y * 0.5 + 0.5);
    float3 horizon = gAmbientColor * 1.8 + float3(0.4, 0.45, 0.5);
    float3 zenith = float3(0.15, 0.35, 0.75);
    float3 col = lerp(horizon, zenith, t);

    // Simple sun glow toward the primary directional light direction (approx: -gLights[0] dir baked at frame time via ambient tint is out of scope here; kept purely atmospheric)
    return float4(col, 1.0);
}
