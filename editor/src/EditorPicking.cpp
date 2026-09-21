// Scene picking for the editor: turning a click into a selected entity.
//
// Previously the only picking in the editor was HandleBrushPicking(), which
// tested brush AABBs and could only ever select *brushes* - never a mesh
// entity. Clicking the cube/sphere/ramp in the viewport did nothing at all, so
// the only way to select anything was the Hierarchy panel.
//
// This file adds real entity picking, shared by the 3D viewport and the Hammer
// 2D panes so both select the same things and agree on what is under the
// cursor.
#include "editor/EditorApp.h"
#include "engine/asset/MeshData.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/ecs/Components.h"
#include "engine/math/Math.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace fw {

namespace {

// Meshes are cached by asset path: picking runs on a click, but the ray/triangle
// pass would otherwise re-read and re-parse every .fwmesh/.obj on the disk each
// time. Mirrors what SoftwareRenderer does internally.
std::unordered_map<std::string, MeshData>& PickCache() {
    static std::unordered_map<std::string, MeshData> cache;
    return cache;
}

const MeshData* MeshForPicking(const std::string& assetPath) {
    if (assetPath.empty()) return nullptr;
    auto& cache = PickCache();
    auto it = cache.find(assetPath);
    if (it != cache.end()) return it->second.vertices.empty() ? nullptr : &it->second;

    MeshData data;
    if (!MeshData::LoadAny(assetPath, data)) {
        cache[assetPath] = MeshData();  // remember the miss so we don't retry every click
        return nullptr;
    }
    auto inserted = cache.emplace(assetPath, std::move(data));
    return inserted.first->second.vertices.empty() ? nullptr : &inserted.first->second;
}

} // namespace

void EditorApp::InvalidatePickCache() { PickCache().clear(); }

// Builds a world-space ray through a pixel of the viewport.
//
// Unprojects the near and far plane points rather than reconstructing the
// direction by hand, so it stays correct for any projection the camera uses
// (the engine renders right-handed with a [0,1] depth range).
Ray EditorApp::ScreenPointToRay(const vec2& screenPixel, const vec2& viewportPos,
                                const vec2& viewportSize, const RenderCamera& cam) const {
    Ray ray;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return ray;

    const vec2 local = screenPixel - viewportPos;
    // Pixel -> NDC. Y flips because screen Y grows downward.
    const float ndcX = (local.x / viewportSize.x) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (local.y / viewportSize.y) * 2.0f;

    const mat4 invViewProj = glm::inverse(cam.ViewProj());
    // z = 0 is the near plane and z = 1 the far plane under [0,1] depth.
    vec4 nearP = invViewProj * vec4(ndcX, ndcY, 0.0f, 1.0f);
    vec4 farP  = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearP.w) < 1e-9f || std::abs(farP.w) < 1e-9f) return ray;
    nearP /= nearP.w;
    farP  /= farP.w;

    ray.origin = vec3(nearP);
    const vec3 dir = vec3(farP) - vec3(nearP);
    const float len = glm::length(dir);
    ray.direction = len > 1e-9f ? dir / len : vec3(0, 0, -1);
    return ray;
}

// Picks the closest entity along a ray.
//
// Two-phase, the standard approach: reject with the world AABB first (cheap),
// then confirm against the actual triangles so you cannot select an object by
// clicking the empty corner of its bounding box. Entities without a loadable
// mesh (lights, cameras, the skylight) fall back to a small clickable sphere so
// they remain selectable in the viewport.
entt::entity EditorApp::PickEntityAt(const Ray& ray, float* outDistance) const {
    Scene& scene = const_cast<EditorApp*>(this)->m_Engine.GetScene();

    entt::entity best = entt::null;
    float bestT = std::numeric_limits<float>::max();

    scene.Each<TransformComponent>([&](Entity e, TransformComponent& tc) {
        const mat4& world = tc.worldMatrix;

        const MeshData* mesh = nullptr;
        if (e.Has<MeshRendererComponent>())
            mesh = MeshForPicking(e.Get<MeshRendererComponent>().meshAsset);

        if (!mesh) {
            // Point entities (lights, cameras): give them a modest pick radius
            // so they can be clicked even though they have no geometry.
            if (!e.Has<LightComponent>() && !e.Has<CameraComponent>()) return;
            const vec3 center(world[3]);
            const float r = 0.45f;
            const vec3 oc = ray.origin - center;
            const float b = glm::dot(oc, ray.direction);
            const float c = glm::dot(oc, oc) - r * r;
            const float disc = b * b - c;
            if (disc < 0.0f) return;
            const float sq = std::sqrt(disc);
            float t = -b - sq;
            if (t < 0.0f) t = -b + sq;
            if (t >= 0.0f && t < bestT) { bestT = t; best = e.Handle(); }
            return;
        }

        // Broad phase: the mesh's local bounds transformed into world space.
        AABB local;
        for (const auto& v : mesh->vertices) local.Grow(v.position);
        float tBox;
        if (!RayAABBIntersect(ray, local.Transformed(world), tBox)) return;
        if (tBox > bestT) return;  // already have something closer

        // Narrow phase: exact triangles. Transform the ray into the mesh's local
        // space once instead of transforming every vertex into world space.
        const mat4 invWorld = glm::inverse(world);
        Ray localRay;
        localRay.origin = vec3(invWorld * vec4(ray.origin, 1.0f));
        localRay.direction = vec3(invWorld * vec4(ray.direction, 0.0f));
        const float dirLen = glm::length(localRay.direction);
        if (dirLen < 1e-9f) return;
        localRay.direction /= dirLen;

        for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            const vec3& p0 = mesh->vertices[mesh->indices[i + 0]].position;
            const vec3& p1 = mesh->vertices[mesh->indices[i + 1]].position;
            const vec3& p2 = mesh->vertices[mesh->indices[i + 2]].position;
            float tLocal;
            if (!RayTriangleIntersect(localRay, p0, p1, p2, tLocal)) continue;
            // Back to world distance: the local ray was renormalised above.
            const vec3 hitWorld = vec3(world * vec4(localRay.At(tLocal), 1.0f));
            const float tWorld = glm::dot(hitWorld - ray.origin, ray.direction);
            if (tWorld >= 0.0f && tWorld < bestT) { bestT = tWorld; best = e.Handle(); }
        }
    });

    if (outDistance) *outDistance = (best == entt::null) ? 0.0f : bestT;
    return best;
}

// Click-to-select in the 3D viewport.
//
// Selecting an entity also selects its brush when it has one, so the Hammer
// panes highlight the same object and the brush tools operate on it.
void EditorApp::HandleViewportPicking() {
    const RenderCamera cam = BuildEditorCamera();
    const Ray ray = ScreenPointToRay(m_PendingPickPos, m_ViewportPos, m_ViewportSize, cam);

    const entt::entity hit = PickEntityAt(ray, nullptr);
    m_SelectedEntity = hit;

    // Keep the brush selection in step with the entity selection.
    m_SelectedBrush = UUID{0};
    if (hit != entt::null) {
        Entity e(hit, &m_Engine.GetScene().Registry());
        if (e.Has<BrushComponent>()) m_SelectedBrush = e.Get<BrushComponent>().brushId;
        FW_LOG_INFO("Selected '%s'", e.Name().c_str());
    }
    m_SoftwarePreviewDirty = true;
}

// Selects whichever entity owns a brush, so clicking a brush in a 2D pane also
// drives the Inspector and the transform gizmo.
void EditorApp::SelectEntityForBrush(UUID brushId) {
    m_SelectedBrush = brushId;
    if (brushId == UUID{0}) return;

    entt::entity owner = entt::null;
    m_Engine.GetScene().Each<BrushComponent>([&](Entity e, BrushComponent& bc) {
        if (bc.brushId == brushId) owner = e.Handle();
    });
    if (owner != entt::null) m_SelectedEntity = owner;
    m_SoftwarePreviewDirty = true;
}

} // namespace fw
