#pragma once
// CPU lightmap baker. Produces a lightmap texture (RGB, HDR-ish stored as
// float then tonemapped to 8-bit on save) per static mesh by hemisphere/
// cosine-weighted sampling against every static LightComponent and every
// other static mesh (simple ambient occlusion + direct lighting + one bounce
// via irradiance caching-lite). This gives baked lighting that blends with
// the renderer's dynamic lights at runtime (MeshRendererComponent::useLightmap).
//
// This is intentionally a straightforward, understandable software baker
// (no GPU compute) so it works identically in the editor (Windows/D3D11) and
// in this repository's headless validation build.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include "engine/asset/MeshData.h"
#include "engine/ecs/Components.h"
#include <vector>
#include <string>
#include <functional>

namespace fw {

class Scene;

struct BakeStaticLight {
    LightType type;
    vec3 position;
    vec3 direction; // for directional/spot
    vec3 color;
    float intensity;
    float range;
    float innerConeDeg, outerConeDeg;
};

struct BakeOccluderTriangle {
    vec3 v0, v1, v2;
};

struct LightmapBakeSettings {
    int resolution = 512;
    int samplesPerTexel = 32;
    int bounces = 1;
    float ambientBoost = 0.15f;
    vec3 skyColor{0.25f, 0.28f, 0.35f};
};

struct LightmapResult {
    int width = 0, height = 0;
    std::vector<vec3> pixels; // linear HDR radiance, RGB
};

class Lightmapper {
public:
    // Bakes a lightmap for a single mesh (using its UV1/lightmap channel)
    // given the full scene's static geometry (for occlusion) and static
    // lights. `progressCallback(percent)` is optional, called periodically.
    static LightmapResult Bake(
        const MeshData& targetMesh,
        const mat4& targetWorldMatrix,
        const std::vector<BakeOccluderTriangle>& occluders,
        const std::vector<BakeStaticLight>& lights,
        const LightmapBakeSettings& settings,
        const std::function<void(float)>& progressCallback = nullptr);

    // Bakes lightmaps for every static MeshRenderer in the scene and writes
    // <asset>.lightmap.png next to each mesh, updating
    // MeshRendererComponent::lightmapAsset/useLightmap.
    static void BakeScene(Scene& scene, const LightmapBakeSettings& settings,
                           const std::function<void(float, const std::string&)>& progressCallback = nullptr);

    // Writes a LightmapResult to disk as a tonemapped PNG.
    static bool SaveAsPNG(const std::string& path, const LightmapResult& result, float exposure = 1.0f);

private:
    static bool RayHitsAnyTriangle(const vec3& origin, const vec3& dir, float maxDist, const std::vector<BakeOccluderTriangle>& tris);
};

} // namespace fw
