// Editor ground grid: a fullscreen triangle whose pixels raycast onto the
// y = 0 plane, so the grid has no geometry and stays sharp at any distance
// (fwidth() gives the correct line width per pixel). This is what makes the
// viewport readable even when a scene contains no meshes yet - the same role
// Hammer's 2D/3D grid plays.
//
// Drawn last, with depth testing enabled but depth writes disabled: geometry
// in front of the grid hides it, and the grid hides nothing.
#include "Common.hlsli"

struct VSOutput {
    float4 clipPos : SV_POSITION;
    float2 ndc     : TEXCOORD0;
};

struct PSOutput {
    float4 color : SV_TARGET;
    float  depth : SV_Depth;
};

VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput o;
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
    o.clipPos = float4(pos, 0.9999, 1.0);
    o.ndc = pos;
    return o;
}

PSOutput PSMain(VSOutput input) {
    PSOutput o;
    o.color = float4(0, 0, 0, 0);
    o.depth = 1.0;

    if (gGridCellSize <= 0.0) discard;

    // Reconstruct the world-space view ray (mul(matrix, vector)!).
    float4 nearPoint = mul(gInvViewProj, float4(input.ndc, 0.0, 1.0));
    float4 farPoint  = mul(gInvViewProj, float4(input.ndc, 1.0, 1.0));
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    float3 dir = normalize(farPoint.xyz - nearPoint.xyz);

    if (dir.y >= -1e-5) discard;               // ray never reaches the ground plane

    float t = -gCameraPos.y / dir.y;           // distance to the y = 0 plane
    if (t <= 0.0) discard;

    float3 world = gCameraPos + dir * t;
    float depth = mul(gViewProj, float4(world, 1.0)).z;
    depth = depth / max(mul(gViewProj, float4(world, 1.0)).w, 1e-6);
    if (depth < 0.0 || depth > 1.0) discard;
    o.depth = depth;

    float cell = max(gGridCellSize, 1e-3);
    float2 coord = world.xz / cell;

    // Distance (in pixels) to the nearest cell line, per axis.
    // ("line" is a reserved HLSL keyword - geometry shader primitives - so the
    // distance variable must not be called that.)
    float2 gridPx = abs(frac(coord - 0.5) - 0.5) / max(fwidth(coord), 1e-6);
    float lineDist = min(gridPx.x, gridPx.y);

    // The world axes are highlighted (X = red, Z = blue).
    float2 worldPx = fwidth(world.xz);
    float axisX = 1.0 - saturate(abs(world.z) / max(worldPx.y * 1.5, 1e-6));
    float axisZ = 1.0 - saturate(abs(world.x) / max(worldPx.x * 1.5, 1e-6));

    // Dark lines read well on light floors; a touch of extra alpha keeps them
    // visible on darker surfaces too.
    float alpha = (1.0 - min(lineDist, 1.0)) * 0.8;
    float3 color = float3(0.10, 0.11, 0.14);
    if (axisX > 0.5) { color = float3(0.85, 0.25, 0.30); alpha = max(alpha, axisX * 0.9); }
    if (axisZ > 0.5) { color = float3(0.30, 0.45, 0.90); alpha = max(alpha, axisZ * 0.9); }

    // Fade to nothing far away so the horizon doesn't alias into noise, and
    // fade out at grazing angles where the grid compresses into sub-pixel
    // detail (same behaviour as the CPU renderer's SampleGrid()).
    float fade = 1.0 - saturate((t - cell * 40.0) / (cell * 100.0));
    float angleFade = saturate(-dir.y * 4.0);
    alpha *= fade * angleFade;

    if (alpha < 0.01) discard;
    o.color = float4(color, alpha);
    return o;
}
