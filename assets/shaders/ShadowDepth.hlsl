// Depth-only pass used to render the shadow map from the primary light's
// point of view.
#include "Common.hlsli"

struct VSInput {
    float3 position : POSITION;
};

float4 VSMain(VSInput input) : SV_POSITION {
    float4 worldPos = mul(float4(input.position, 1.0), gWorld);
    return mul(worldPos, gLightViewProj);
}
