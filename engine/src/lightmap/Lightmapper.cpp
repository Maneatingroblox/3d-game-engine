#include "engine/lightmap/Lightmapper.h"
#include "engine/scene/Scene.h"
#include "engine/core/Log.h"
#include <random>
#include <algorithm>
#include <cmath>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fw {

static std::mt19937& RngForThread() {
    thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

// Cosine-weighted hemisphere sample around normal `n`.
static vec3 SampleCosineHemisphere(const vec3& n) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float u1 = dist(RngForThread());
    float u2 = dist(RngForThread());
    float r = std::sqrt(u1);
    float theta = 2.0f * kPi * u2;
    float x = r * std::cos(theta);
    float y = r * std::sin(theta);
    float z = std::sqrt(std::max(0.0f, 1.0f - u1));

    vec3 up = std::abs(n.z) < 0.999f ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = glm::normalize(glm::cross(up, n));
    vec3 bitangent = glm::cross(n, tangent);
    return glm::normalize(tangent * x + bitangent * y + n * z);
}

bool Lightmapper::RayHitsAnyTriangle(const vec3& origin, const vec3& dir, float maxDist, const std::vector<BakeOccluderTriangle>& tris) {
    Ray ray{ origin, dir };
    for (auto& t : tris) {
        float dist;
        if (RayTriangleIntersect(ray, t.v0, t.v1, t.v2, dist)) {
            if (dist < maxDist - 1e-3f) return true;
        }
    }
    return false;
}

static float AttenuationForLight(const BakeStaticLight& light, const vec3& point, vec3& outDir, float& outMaxDist) {
    if (light.type == LightType::Directional) {
        outDir = -glm::normalize(light.direction);
        outMaxDist = 1e6f;
        return 1.0f;
    }
    vec3 toLight = light.position - point;
    float dist = glm::length(toLight);
    outDir = dist > 1e-6f ? toLight / dist : vec3(0, 1, 0);
    outMaxDist = dist;
    if (dist > light.range) return 0.0f;

    float atten = glm::clamp(1.0f - (dist / light.range), 0.0f, 1.0f);
    atten *= atten;

    if (light.type == LightType::Spot) {
        float cosAngle = glm::dot(-outDir, glm::normalize(light.direction));
        float innerCos = std::cos(Radians(light.innerConeDeg));
        float outerCos = std::cos(Radians(light.outerConeDeg));
        float spot = glm::clamp((cosAngle - outerCos) / std::max(innerCos - outerCos, 1e-4f), 0.0f, 1.0f);
        atten *= spot * spot;
    }
    return atten;
}

LightmapResult Lightmapper::Bake(
    const MeshData& targetMesh,
    const mat4& targetWorldMatrix,
    const std::vector<BakeOccluderTriangle>& occluders,
    const std::vector<BakeStaticLight>& lights,
    const LightmapBakeSettings& settings,
    const std::function<void(float)>& progressCallback)
{
    LightmapResult result;
    result.width = result.height = settings.resolution;
    result.pixels.assign((size_t)result.width * result.height, vec3(0.0f));
    std::vector<u8> coverage((size_t)result.width * result.height, 0);

    mat3 normalMatrix = glm::transpose(glm::inverse(mat3(targetWorldMatrix)));

    size_t triCount = targetMesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; t++) {
        u32 i0 = targetMesh.indices[t * 3 + 0];
        u32 i1 = targetMesh.indices[t * 3 + 1];
        u32 i2 = targetMesh.indices[t * 3 + 2];
        const Vertex& v0 = targetMesh.vertices[i0];
        const Vertex& v1 = targetMesh.vertices[i1];
        const Vertex& v2 = targetMesh.vertices[i2];

        vec3 wp0 = vec3(targetWorldMatrix * vec4(v0.position, 1.0f));
        vec3 wp1 = vec3(targetWorldMatrix * vec4(v1.position, 1.0f));
        vec3 wp2 = vec3(targetWorldMatrix * vec4(v2.position, 1.0f));
        vec3 wn0 = glm::normalize(normalMatrix * v0.normal);
        vec3 wn1 = glm::normalize(normalMatrix * v1.normal);
        vec3 wn2 = glm::normalize(normalMatrix * v2.normal);

        // Rasterize the triangle's UV1 footprint into texel space and shade
        // each covered texel using barycentric-interpolated world pos/normal.
        vec2 uv0 = v0.uv1 * (float)result.width;
        vec2 uv1 = v1.uv1 * (float)result.width;
        vec2 uv2 = v2.uv1 * (float)result.width;

        int minX = std::max(0, (int)std::floor(std::min({uv0.x, uv1.x, uv2.x})));
        int maxX = std::min(result.width - 1, (int)std::ceil(std::max({uv0.x, uv1.x, uv2.x})));
        int minY = std::max(0, (int)std::floor(std::min({uv0.y, uv1.y, uv2.y})));
        int maxY = std::min(result.height - 1, (int)std::ceil(std::max({uv0.y, uv1.y, uv2.y})));

        float area = (uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y);
        if (std::abs(area) < 1e-8f) continue;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                vec2 p(x + 0.5f, y + 0.5f);
                float w0 = ((uv1.x - p.x) * (uv2.y - p.y) - (uv2.x - p.x) * (uv1.y - p.y)) / area;
                float w1 = ((uv2.x - p.x) * (uv0.y - p.y) - (uv0.x - p.x) * (uv2.y - p.y)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < -0.01f || w1 < -0.01f || w2 < -0.01f) continue;

                vec3 worldPos = w0 * wp0 + w1 * wp1 + w2 * wp2;
                vec3 worldNormal = glm::normalize(w0 * wn0 + w1 * wn1 + w2 * wn2);
                worldPos += worldNormal * 0.02f; // bias to avoid self-shadowing

                vec3 accum(0.0f);
                // Direct lighting from every static light
                for (auto& light : lights) {
                    vec3 dir; float maxDist;
                    float atten = AttenuationForLight(light, worldPos, dir, maxDist);
                    if (atten <= 0.0f) continue;
                    float ndotl = std::max(0.0f, glm::dot(worldNormal, dir));
                    if (ndotl <= 0.0f) continue;
                    if (RayHitsAnyTriangle(worldPos, dir, maxDist, occluders)) continue; // shadowed
                    accum += light.color * (light.intensity * atten * ndotl);
                }

                // Ambient occlusion + sky contribution via cosine-weighted hemisphere sampling
                int aoSamples = std::max(1, settings.samplesPerTexel);
                float aoUnoccluded = 0.0f;
                vec3 skyAccum(0.0f);
                for (int s = 0; s < aoSamples; s++) {
                    vec3 sampleDir = SampleCosineHemisphere(worldNormal);
                    bool occluded = RayHitsAnyTriangle(worldPos, sampleDir, 50.0f, occluders);
                    if (!occluded) {
                        aoUnoccluded += 1.0f;
                        skyAccum += settings.skyColor;
                    }
                }
                float ao = aoUnoccluded / aoSamples;
                accum += (skyAccum / (float)aoSamples) * settings.ambientBoost + vec3(settings.ambientBoost * 0.05f) * ao;

                size_t idx = (size_t)y * result.width + x;
                result.pixels[idx] = accum;
                coverage[idx] = 1;
            }
        }
        if (progressCallback && (t % 64 == 0)) progressCallback((float)t / (float)triCount);
    }

    // Dilate to fill unwrap seams/padding (simple box-dilate over uncovered texels).
    for (int pass = 0; pass < 4; pass++) {
        std::vector<vec3> next = result.pixels;
        std::vector<u8> nextCov = coverage;
        for (int y = 0; y < result.height; y++) {
            for (int x = 0; x < result.width; x++) {
                size_t idx = (size_t)y * result.width + x;
                if (coverage[idx]) continue;
                vec3 sum(0.0f); int count = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= result.width || ny >= result.height) continue;
                    size_t nidx = (size_t)ny * result.width + nx;
                    if (coverage[nidx]) { sum += result.pixels[nidx]; count++; }
                }
                if (count > 0) { next[idx] = sum / (float)count; nextCov[idx] = 1; }
            }
        }
        result.pixels = next;
        coverage = nextCov;
    }

    if (progressCallback) progressCallback(1.0f);
    return result;
}

bool Lightmapper::SaveAsPNG(const std::string& path, const LightmapResult& result, float exposure) {
    std::vector<u8> rgb((size_t)result.width * result.height * 3);
    for (size_t i = 0; i < result.pixels.size(); i++) {
        vec3 c = result.pixels[i] * exposure;
        // simple Reinhard tonemap + gamma correct
        c = c / (c + vec3(1.0f));
        c = glm::pow(c, vec3(1.0f / 2.2f));
        rgb[i * 3 + 0] = (u8)glm::clamp(c.r * 255.0f, 0.0f, 255.0f);
        rgb[i * 3 + 1] = (u8)glm::clamp(c.g * 255.0f, 0.0f, 255.0f);
        rgb[i * 3 + 2] = (u8)glm::clamp(c.b * 255.0f, 0.0f, 255.0f);
    }
    return stbi_write_png(path.c_str(), result.width, result.height, 3, rgb.data(), result.width * 3) != 0;
}

void Lightmapper::BakeScene(Scene& scene, const LightmapBakeSettings& settings,
                             const std::function<void(float, const std::string&)>& progressCallback) {
    // Gather static occluder triangles from every static mesh renderer.
    std::vector<BakeOccluderTriangle> occluders;
    std::vector<BakeStaticLight> lights;

    scene.Each<TransformComponent, LightComponent>([&](Entity, TransformComponent& tc, LightComponent& lc) {
        if (!lc.isStatic) return;
        BakeStaticLight bl;
        bl.type = lc.type;
        bl.position = tc.local.position;
        bl.direction = tc.local.Forward();
        bl.color = lc.color;
        bl.intensity = lc.intensity;
        bl.range = lc.range;
        bl.innerConeDeg = lc.innerConeDeg;
        bl.outerConeDeg = lc.outerConeDeg;
        lights.push_back(bl);
    });

    struct Target { entt::entity e; MeshData mesh; mat4 world; std::string asset; };
    std::vector<Target> targets;

    scene.Each<TransformComponent, MeshRendererComponent>([&](Entity e, TransformComponent& tc, MeshRendererComponent& mr) {
        MeshData mesh;
        if (!MeshData::LoadFWMesh(mr.meshAsset, mesh)) {
            std::string err;
            if (!MeshData::LoadOBJ(mr.meshAsset, mesh, &err)) return;
        }
        if (mesh.vertices.empty()) return;
        mesh.GenerateLightmapUVs(settings.resolution);

        mat3 nm = glm::transpose(glm::inverse(mat3(tc.worldMatrix)));
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            occluders.push_back({
                vec3(tc.worldMatrix * vec4(mesh.vertices[mesh.indices[t]].position, 1.0f)),
                vec3(tc.worldMatrix * vec4(mesh.vertices[mesh.indices[t+1]].position, 1.0f)),
                vec3(tc.worldMatrix * vec4(mesh.vertices[mesh.indices[t+2]].position, 1.0f)),
            });
        }
        targets.push_back({ e.Handle(), mesh, tc.worldMatrix, mr.meshAsset });
    });

    for (size_t i = 0; i < targets.size(); i++) {
        auto& target = targets[i];
        LightmapResult result = Lightmapper::Bake(target.mesh, target.world, occluders, lights, settings,
            [&](float p) { if (progressCallback) progressCallback((i + p) / (float)targets.size(), target.asset); });

        std::string lmPath = target.asset + ".lightmap.png";
        SaveAsPNG(lmPath, result);

        // Re-save the mesh with generated UV1 so the lightmap coordinates persist.
        MeshData::SaveFWMesh(target.asset, target.mesh);

        Entity e(target.e, &scene.Registry());
        auto& mr = e.Get<MeshRendererComponent>();
        mr.useLightmap = true;
        mr.lightmapAsset = lmPath;
    }

    if (progressCallback) progressCallback(1.0f, "done");
    FW_LOG_INFO("Lightmap bake complete: %zu mesh(es), %zu static light(s)", targets.size(), lights.size());
}

} // namespace fw
