// Depth-only pass used to render the shadow map from the primary light's
// point of view. Matrix convention: mul(matrix, vector) - see Common.hlsli.
// The projection is RH with depth range [0,1] (RH_ZO), matching D3D11.
#include "Common.hlsli"

struct VSInput {
    float3 position : POSITION;
};

float4 VSMain(VSInput input) : SV_POSITION {
    float4 worldPos = mul(gWorld, float4(input.position, 1.0));
    return mul(gLightViewProj, worldPos);
}
