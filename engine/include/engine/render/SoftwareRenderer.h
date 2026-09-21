#pragma once
// Software (CPU) reference renderer.
//
// Why this exists:
//   1. The editor's viewport is a render target. On Windows that target is a
//      D3D11 texture; on any other platform (and in automated tests) there is
//      no GPU, so the viewport used to come out completely blank - which made
//      "the editor shows nothing" impossible to catch without a Windows box.
//   2. It renders the *real* Scene through the *real* camera math
//      (RenderTypes.h) and mirrors the HLSL passes (skybox / grid / forward
//      mesh shading) so a CPU-rendered image is a faithful sanity check that
//      geometry, lighting and camera setup produce something visible.
//
// It is intentionally simple: no shadows, no post-processing, one thread, and
// no texture sampling (material colours only). It is a diagnostic/preview
// rasterizer, not a replacement for the D3D11 renderer.

#include "engine/core/Base.h"
#include "engine/render/RenderTypes.h"
#include <vector>

namespace fw {

class Scene;

// An RGBA8, top-down, row-major CPU image.
struct SoftwareImage {
    int width = 0;
    int height = 0;
    std::vector<u8> rgba;

    void Resize(int w, int h) {
        width = w > 0 ? w : 0;
        height = h > 0 ? h : 0;
        rgba.assign((size_t)width * (size_t)height * 4u, 0);
    }
    bool Empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    u8* Data() { return rgba.data(); }
    const u8* Data() const { return rgba.data(); }
};

// Statistics about the last render, used by tests ("is the viewport blank?")
// and by the editor's viewport overlay.
struct SoftwareRenderStats {
    int width = 0, height = 0;
    int geometryPixels = 0;  // pixels written by mesh geometry (excludes sky/grid)
    int gridPixels = 0;
    int drawnTriangles = 0;
    int culledTriangles = 0;
    int meshEntities = 0;
    int lightCount = 0;

    // Fraction of the image covered by actual scene geometry. Tests use this
    // to assert the viewport is not blank (a sky+grid-only image has ~0).
    float GeometryCoverage() const {
        const int total = width * height;
        return total > 0 ? (float)geometryPixels / (float)total : 0.0f;
    }
};

class SoftwareRenderer {
public:
    // Renders `scene` from `camera` into `image` (resized as needed).
    static void RenderScene(Scene& scene, const RenderCamera& camera, const RenderSettings& settings,
                            SoftwareImage& image, SoftwareRenderStats* outStats = nullptr);

    // Drops the cached CPU meshes/materials (call when assets change).
    static void ClearCaches();
};

} // namespace fw
