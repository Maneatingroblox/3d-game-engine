#include "engine/render/SoftwareRenderer.h"
#include "engine/render/MaterialSystem.h"
#include "engine/asset/MeshData.h"
#include "engine/scene/Scene.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace fw {

namespace {

// ---------------------------------------------------------------------------
// Asset caches (CPU meshes / material colours), keyed by asset path.
// ---------------------------------------------------------------------------
std::unordered_map<std::string, MeshData> g_MeshCache;
std::unordered_map<std::string, vec4> g_MaterialAlbedo;
std::unordered_map<std::string, vec3> g_MaterialEmissive;

const MeshData* GetMesh(const std::string& assetPath) {
    if (assetPath.empty()) return nullptr;
    auto it = g_MeshCache.find(assetPath);
    if (it != g_MeshCache.end()) return it->second.vertices.empty() ? nullptr : &it->second;

    MeshData data;
    if (!MeshData::LoadAny(assetPath, data) || data.vertices.empty()) {
        FW_LOG_WARN("SoftwareRenderer: could not load mesh '%s'", assetPath.c_str());
        g_MeshCache[assetPath] = MeshData(); // remember the miss
        return nullptr;
    }
    auto inserted = g_MeshCache.emplace(assetPath, std::move(data));
    return &inserted.first->second;
}

struct CpuMaterial {
    vec4 albedo{1.0f};
    vec3 emissive{0.0f};
};

CpuMaterial GetMaterial(const std::string& assetPath) {
    CpuMaterial out;
    if (assetPath.empty()) return out;

    auto it = g_MaterialAlbedo.find(assetPath);
    if (it != g_MaterialAlbedo.end()) {
        out.albedo = it->second;
        auto e = g_MaterialEmissive.find(assetPath);
        if (e != g_MaterialEmissive.end()) out.emissive = e->second;
        return out;
    }

    // MaterialSystem's loader is platform independent - only Bind() needs D3D.
    static MaterialSystem loader(nullptr, nullptr);
    if (Material* mat = loader.Load(assetPath)) {
        out.albedo = mat->albedoColor;
        out.emissive = mat->emissiveColor * mat->emissiveStrength;
    }
    g_MaterialAlbedo[assetPath] = out.albedo;
    g_MaterialEmissive[assetPath] = out.emissive;
    return out;
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------
struct CpuLight {
    int type = 1; // 0 = directional, 1 = point, 2 = spot
    vec3 positionOrDir{0.0f};
    vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerCos = 1.0f;
    float outerCos = 1.0f;
    vec3 spotDir{0.0f, 0.0f, -1.0f};
};

struct Framebuffer {
    int width = 0, height = 0;
    std::vector<vec3> color;
    std::vector<float> depth;

    void Resize(int w, int h) {
        width = w;
        height = h;
        color.assign((size_t)w * h, vec3(0.0f));
        depth.assign((size_t)w * h, 1.0f);
    }
    int Index(int x, int y) const { return y * width + x; }
};

// Identical maths to assets/shaders/Skybox.hlsl.
vec3 SkyColor(const vec3& dir, const vec3& ambient) {
    const float t = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const vec3 horizon = ambient * 1.8f + vec3(0.4f, 0.45f, 0.5f);
    const vec3 zenith(0.15f, 0.35f, 0.75f);
    return glm::mix(horizon, zenith, t);
}

struct VSOut {
    vec4 clip{0.0f};
    vec3 worldPos{0.0f};
    vec3 normal{0.0f};
    vec2 uv0{0.0f};
    vec4 color{1.0f};
};

VSOut LerpVS(const VSOut& a, const VSOut& b, float t) {
    VSOut o;
    o.clip = glm::mix(a.clip, b.clip, t);
    o.worldPos = glm::mix(a.worldPos, b.worldPos, t);
    o.normal = glm::mix(a.normal, b.normal, t);
    o.uv0 = glm::mix(a.uv0, b.uv0, t);
    o.color = glm::mix(a.color, b.color, t);
    return o;
}

vec3 ShadeFragment(const VSOut& v, const vec3& baseColor, const vec3& emissive,
                   const std::vector<CpuLight>& lights, const vec3& eye, const RenderSettings& settings) {
    const float nLen = glm::length(v.normal);
    const vec3 N = nLen > 1e-6f ? v.normal / nLen : vec3(0, 1, 0);
    const vec3 V = glm::normalize(eye - v.worldPos);

    vec3 direct(0.0f);
    for (const auto& l : lights) {
        vec3 L;
        float attenuation = 1.0f;
        if (l.type == 0) {
            L = glm::normalize(-l.positionOrDir);
        } else {
            const vec3 toLight = l.positionOrDir - v.worldPos;
            const float dist = glm::length(toLight);
            L = toLight / std::max(dist, 1e-5f);
            attenuation = glm::clamp(1.0f - dist / std::max(l.range, 1e-3f), 0.0f, 1.0f);
            attenuation *= attenuation;
            if (l.type == 2) {
                const float cosAngle = glm::dot(-L, glm::normalize(l.spotDir));
                const float spot = glm::clamp((cosAngle - l.outerCos) / std::max(l.innerCos - l.outerCos, 1e-4f), 0.0f, 1.0f);
                attenuation *= spot * spot;
            }
        }

        const float NdotL = std::max(glm::dot(N, L), 0.0f);
        if (NdotL <= 0.0f) continue;

        const vec3 H = glm::normalize(V + L);
        const float spec = std::pow(std::max(glm::dot(N, H), 0.0f), 48.0f) * 0.15f;
        direct += (baseColor / kPi + vec3(spec)) * l.color * l.intensity * attenuation * NdotL;
    }

    const vec3 ambient = settings.ambientColor * settings.ambientIntensity * baseColor;
    return ambient + direct + emissive;
}

// Analytic ground grid, mirroring assets/shaders/Grid.hlsl.
struct GridSample {
    float alpha = 0.0f;
    vec3 color{0.0f};
};

GridSample SampleGrid(const vec3& world, float t, float pixelWorldSize, float cell,
                      float fadeStart, float fadeEnd, float grazing) {
    GridSample s;
    if (cell <= 0.0f) return s;

    const float coordX = world.x / cell;
    const float coordZ = world.z / cell;
    const float distToLineX = std::abs((coordX - std::floor(coordX)) - 0.5f) * cell;
    const float distToLineZ = std::abs((coordZ - std::floor(coordZ)) - 0.5f) * cell;

    const float halfBand = std::max(pixelWorldSize * 0.5f, 1e-4f);
    const float lineX = 1.0f - glm::clamp(distToLineX / halfBand, 0.0f, 1.0f);
    const float lineZ = 1.0f - glm::clamp(distToLineZ / halfBand, 0.0f, 1.0f);

    const float axisHalf = std::max(pixelWorldSize * 0.75f, 1e-4f);
    const bool onAxisX = std::abs(world.z) < axisHalf;
    const bool onAxisZ = std::abs(world.x) < axisHalf;

    float alpha = std::max(lineX, lineZ) * 0.8f;
    vec3 color(0.10f, 0.11f, 0.14f);
    if (onAxisX) { alpha = 1.0f; color = vec3(0.80f, 0.25f, 0.28f); } // X axis: red
    if (onAxisZ) { alpha = 1.0f; color = vec3(0.28f, 0.45f, 0.85f); } // Z axis: blue

    const float fade = 1.0f - glm::clamp((t - fadeStart) / std::max(fadeEnd - fadeStart, 1e-3f), 0.0f, 1.0f);
    // Grazing angles compress the grid into sub-pixel noise; fade it out like
    // a real editor grid does toward the horizon.
    const float angleFade = glm::clamp(grazing * 4.0f, 0.0f, 1.0f);
    s.alpha = alpha * fade * angleFade;
    s.color = color;
    return s;
}

// Clips a convex polygon (in clip space) against one half-space.
template <typename InsideFn, typename DistFn>
std::vector<VSOut> ClipPolygon(const std::vector<VSOut>& poly, InsideFn inside, DistFn dist) {
    std::vector<VSOut> out;
    const size_t n = poly.size();
    if (n == 0) return out;
    for (size_t i = 0; i < n; i++) {
        const VSOut& cur = poly[i];
        const VSOut& nxt = poly[(i + 1) % n];
        const bool curIn = inside(cur);
        const bool nxtIn = inside(nxt);
        if (curIn) out.push_back(cur);
        if (curIn != nxtIn) {
            const float d0 = dist(cur);
            const float d1 = dist(nxt);
            const float denom = d0 - d1;
            const float tt = std::abs(denom) > 1e-9f ? d0 / denom : 0.0f;
            out.push_back(LerpVS(cur, nxt, glm::clamp(tt, 0.0f, 1.0f)));
        }
    }
    return out;
}

// A triangle after perspective divide.
struct ScreenVertex {
    float sx = 0.0f, sy = 0.0f;  // pixels, y down
    float w = 1.0f;              // clip-space w (for perspective-correct interpolation)
    float z = 1.0f;              // NDC depth in [0,1]
    VSOut attrs;
};

} // namespace

void SoftwareRenderer::ClearCaches() {
    g_MeshCache.clear();
    g_MaterialAlbedo.clear();
    g_MaterialEmissive.clear();
}

void SoftwareRenderer::RenderScene(Scene& scene, const RenderCamera& camera, const RenderSettings& settings,
                                   SoftwareImage& image, SoftwareRenderStats* outStats) {
    const int width = std::max(image.width, 1);
    const int height = std::max(image.height, 1);
    if (image.Empty()) image.Resize(width, height);

    Framebuffer fb;
    fb.Resize(width, height);

    RenderCamera cam = camera;
    const mat4 viewProj = cam.proj * cam.view;
    const mat4 invViewProj = glm::inverse(viewProj);
    const vec3 eye = cam.position;
    if (cam.proj[2][2] == 0.0f) { // sanity: never divide by a broken projection
        cam.proj = MakeProjectionMatrix(60.0f, (float)width / (float)height, 0.05f, 2000.0f);
    }

    // ---- lights ---------------------------------------------------------
    std::vector<CpuLight> lights;
    scene.Each<TransformComponent, LightComponent>([&](Entity, TransformComponent& tc, LightComponent& lc) {
        if (lights.size() >= 32) return;
        CpuLight l;
        l.type = lc.type == LightType::Directional ? 0 : (lc.type == LightType::Point ? 1 : 2);
        l.positionOrDir = lc.type == LightType::Directional ? tc.local.Forward() : tc.local.position;
        l.color = lc.color;
        l.intensity = lc.intensity;
        l.range = lc.range;
        l.innerCos = std::cos(Radians(lc.innerConeDeg));
        l.outerCos = std::cos(Radians(lc.outerConeDeg));
        l.spotDir = tc.local.Forward();
        lights.push_back(l);
    });

    SoftwareRenderStats stats;
    stats.width = width;
    stats.height = height;
    stats.lightCount = (int)lights.size();

    // ---- background + ground grid (full-screen analytic pass) -----------
    const bool drawGrid = settings.drawGrid && settings.gridCellSize > 0.0f;
    int gridPixels = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const vec2 ndc((x + 0.5f) / (float)width * 2.0f - 1.0f,
                           1.0f - (y + 0.5f) / (float)height * 2.0f);
            vec4 farPoint = invViewProj * vec4(ndc.x, ndc.y, 1.0f, 1.0f);
            farPoint /= farPoint.w;
            const vec3 dir = glm::normalize(vec3(farPoint) - eye);

            vec3 col = SkyColor(dir, settings.ambientColor);
            float depth = 1.0f;

            if (drawGrid && dir.y < -1e-5f) {
                const float t = -eye.y / dir.y;
                if (t > 0.0f) {
                    const vec3 world = eye + dir * t;
                    const float pixelWorldSize = 2.0f * t / std::max(cam.proj[1][1], 1e-6f) / (float)height;
                    const float cell = settings.gridCellSize;
                    GridSample gs = SampleGrid(world, t, pixelWorldSize, cell, cell * 60.0f, cell * 140.0f, -dir.y);
                    if (gs.alpha > 0.02f) {
                        col = glm::mix(col, gs.color, glm::clamp(gs.alpha, 0.0f, 1.0f));
                        const vec4 clip = viewProj * vec4(world, 1.0f);
                        const float ndcZ = clip.w > 1e-6f ? clip.z / clip.w : 1.0f;
                        if (ndcZ >= 0.0f && ndcZ <= 1.0f) {
                            depth = ndcZ;
                            gridPixels++;
                        }
                    }
                }
            }

            const int i = fb.Index(x, y);
            fb.color[i] = col;
            fb.depth[i] = depth;
        }
    }
    stats.gridPixels = gridPixels;

    // ---- geometry --------------------------------------------------------
    int geometryPixels = 0;
    int drawnTris = 0, culledTris = 0, meshEntities = 0;

    scene.Each<TransformComponent, MeshRendererComponent>([&](Entity, TransformComponent& tc, MeshRendererComponent& mr) {
        const MeshData* mesh = GetMesh(mr.meshAsset);
        if (!mesh || mesh->indices.empty()) return;
        meshEntities++;

        const mat4 world = tc.worldMatrix;
        const mat4 worldViewProj = viewProj * world;
        const mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(world)));

        std::vector<VSOut> verts(mesh->vertices.size());
        for (size_t i = 0; i < mesh->vertices.size(); i++) {
            const Vertex& src = mesh->vertices[i];
            VSOut& v = verts[i];
            const vec4 wp = world * vec4(src.position, 1.0f);
            v.worldPos = vec3(wp);
            v.clip = worldViewProj * vec4(src.position, 1.0f);
            v.normal = normalMatrix * src.normal;
            v.uv0 = src.uv0;
            v.color = src.color;
        }

        const size_t subCount = mesh->subMeshes.empty() ? 1 : mesh->subMeshes.size();
        for (size_t s = 0; s < subCount; s++) {
            SubMesh sub;
            if (mesh->subMeshes.empty()) {
                sub.indexStart = 0;
                sub.indexCount = (u32)mesh->indices.size();
                sub.materialIndex = 0;
            } else {
                sub = mesh->subMeshes[s];
            }

            std::string matPath;
            if (sub.materialIndex >= 0 && sub.materialIndex < (int)mr.materialSlots.size())
                matPath = mr.materialSlots[(size_t)sub.materialIndex];
            const CpuMaterial mat = GetMaterial(matPath);

            for (u32 t = 0; t + 2 < sub.indexCount; t += 3) {
                const u32 i0 = mesh->indices[sub.indexStart + t + 0];
                const u32 i1 = mesh->indices[sub.indexStart + t + 1];
                const u32 i2 = mesh->indices[sub.indexStart + t + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

                std::vector<VSOut> poly = { verts[i0], verts[i1], verts[i2] };
                // Near plane: z >= 0 in clip space (RH, Direct3D-style depth).
                // Vertices behind the camera fail both tests and are removed.
                poly = ClipPolygon(poly, [](const VSOut& v) { return v.clip.z >= 0.0f; },
                                   [](const VSOut& v) { return v.clip.z; });
                poly = ClipPolygon(poly, [](const VSOut& v) { return v.clip.w > 1e-5f; },
                                   [](const VSOut& v) { return v.clip.w - 1e-5f; });
                if (poly.size() < 3) { culledTris++; continue; }

                for (size_t k = 1; k + 1 < poly.size(); k++) {
                    ScreenVertex tri[3];
                    const VSOut* srcVerts[3] = { &poly[0], &poly[k], &poly[k + 1] };
                    for (int vi = 0; vi < 3; vi++) {
                        const VSOut& v = *srcVerts[vi];
                        const float invW = 1.0f / v.clip.w;
                        tri[vi].w = v.clip.w;
                        tri[vi].z = v.clip.z * invW;                 // NDC depth [0,1]
                        tri[vi].sx = (v.clip.x * invW * 0.5f + 0.5f) * (float)width;
                        tri[vi].sy = (0.5f - v.clip.y * invW * 0.5f) * (float)height;
                        tri[vi].attrs = v;
                    }

                    // Screen-space signed area: front faces (outward-facing
                    // counter-clockwise triangles) are negative here because
                    // screen Y points down - the same rule D3D11's default
                    // rasterizer uses, so culling matches the GPU path.
                    const float area = (tri[1].sx - tri[0].sx) * (tri[2].sy - tri[0].sy) -
                                       (tri[1].sy - tri[0].sy) * (tri[2].sx - tri[0].sx);
                    if (std::abs(area) < 1e-6f) continue;
                    if (area >= 0.0f) { culledTris++; continue; } // back face
                    const float invArea = 1.0f / area;

                    int minX = (int)std::floor(std::min({ tri[0].sx, tri[1].sx, tri[2].sx }));
                    int maxX = (int)std::ceil(std::max({ tri[0].sx, tri[1].sx, tri[2].sx }));
                    int minY = (int)std::floor(std::min({ tri[0].sy, tri[1].sy, tri[2].sy }));
                    int maxY = (int)std::ceil(std::max({ tri[0].sy, tri[1].sy, tri[2].sy }));
                    minX = std::max(minX, 0); minY = std::max(minY, 0);
                    maxX = std::min(maxX, width - 1); maxY = std::min(maxY, height - 1);
                    if (minX > maxX || minY > maxY) continue;

                    drawnTris++;

                    for (int py = minY; py <= maxY; py++) {
                        for (int px = minX; px <= maxX; px++) {
                            const float fx = px + 0.5f, fy = py + 0.5f;
                            const float l0 = ((tri[2].sx - tri[1].sx) * (fy - tri[1].sy) -
                                              (tri[2].sy - tri[1].sy) * (fx - tri[1].sx)) * invArea;
                            const float l1 = ((tri[0].sx - tri[2].sx) * (fy - tri[2].sy) -
                                              (tri[0].sy - tri[2].sy) * (fx - tri[2].sx)) * invArea;
                            const float l2 = ((tri[1].sx - tri[0].sx) * (fy - tri[0].sy) -
                                              (tri[1].sy - tri[0].sy) * (fx - tri[0].sx)) * invArea;
                            if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) continue;

                            if (settings.wireframe && std::min({ l0, l1, l2 }) > 0.03f) continue;

                            // Perspective-correct barycentrics.
                            const float pw0 = l0 / tri[0].w, pw1 = l1 / tri[1].w, pw2 = l2 / tri[2].w;
                            const float invSum = 1.0f / std::max(pw0 + pw1 + pw2, 1e-12f);
                            const float b0 = pw0 * invSum, b1 = pw1 * invSum, b2 = pw2 * invSum;

                            const float depth = tri[0].z * b0 + tri[1].z * b1 + tri[2].z * b2;
                            if (depth < 0.0f || depth > 1.0f) continue;

                            const int idx = fb.Index(px, py);
                            if (depth >= fb.depth[idx]) continue;

                            VSOut frag;
                            frag.worldPos = tri[0].attrs.worldPos * b0 + tri[1].attrs.worldPos * b1 + tri[2].attrs.worldPos * b2;
                            frag.normal = tri[0].attrs.normal * b0 + tri[1].attrs.normal * b1 + tri[2].attrs.normal * b2;
                            frag.uv0 = tri[0].attrs.uv0 * b0 + tri[1].attrs.uv0 * b1 + tri[2].attrs.uv0 * b2;
                            frag.color = tri[0].attrs.color * b0 + tri[1].attrs.color * b1 + tri[2].attrs.color * b2;

                            const vec3 base = vec3(mat.albedo) * vec3(frag.color);
                            fb.color[idx] = ShadeFragment(frag, base, mat.emissive, lights, eye, settings);
                            fb.depth[idx] = depth;
                            geometryPixels++;
                        }
                    }
                }
            }
        }
    });

    // ---- resolve to RGBA8 ------------------------------------------------
    image.Resize(width, height);
    u8* dst = image.Data();
    for (int i = 0; i < width * height; i++) {
        // Same tonemap + gamma as FWToDisplay() in assets/shaders/Common.hlsli.
        const vec3 linear = glm::max(fb.color[i], vec3(0.0f));
        const vec3 mapped = glm::clamp(linear / (linear + vec3(1.0f)) * 1.12f, vec3(0.0f), vec3(1.0f));
        const vec3 c = glm::pow(mapped, vec3(1.0f / 2.2f));
        dst[i * 4 + 0] = (u8)(c.r * 255.0f + 0.5f);
        dst[i * 4 + 1] = (u8)(c.g * 255.0f + 0.5f);
        dst[i * 4 + 2] = (u8)(c.b * 255.0f + 0.5f);
        dst[i * 4 + 3] = 255;
    }

    stats.geometryPixels = geometryPixels;
    stats.drawnTriangles = drawnTris;
    stats.culledTriangles = culledTris;
    stats.meshEntities = meshEntities;
    if (outStats) *outStats = stats;
}

} // namespace fw
