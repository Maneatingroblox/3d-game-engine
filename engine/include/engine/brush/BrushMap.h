#pragma once
// A BrushMap holds all Hammer-mode brushes for a scene/level. Brushes are
// authored/edited here; each brush is mirrored into the ECS as an entity
// with a BrushComponent + MeshRendererComponent (compiled mesh) +
// ColliderComponent (TriangleMesh/ConvexHull), so scripts, physics and
// rendering all treat brush geometry exactly like any other entity -
// "Hammer mode" is just another way to *author* geometry for the same
// running engine/scene.

#include "engine/core/Base.h"
#include "engine/core/UUID.h"
#include "engine/brush/Brush.h"
#include <entt/entt.hpp>
#include <unordered_map>
#include <unordered_set>

namespace fw {

class Scene;

class BrushMap {
public:
    Brush& AddBrush(Brush brush);
    void RemoveBrush(UUID id);
    Brush* FindBrush(UUID id);
    std::vector<Brush>& Brushes() { return m_Brushes; }
    const std::vector<Brush>& Brushes() const { return m_Brushes; }

    // Compiles every brush into a mesh asset on disk (assets/generated/brush_<id>.fwmesh)
    // and creates/updates the corresponding ECS entity (MeshRenderer + Collider).
    // Call after any brush edit; incremental (skips unchanged brushes when
    // `dirtyOnly` is true and the brush hasn't been touched since last compile).
    void CompileToScene(Scene& scene, const std::string& generatedMeshDir, bool dirtyOnly = true);

    void MarkDirty(UUID id) { m_Dirty.insert(id); }
    void MarkAllDirty();

    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

private:
    std::vector<Brush> m_Brushes;
    std::unordered_map<u64, entt::entity> m_BrushToEntity;
    std::unordered_set<u64> m_Dirty;
};

} // namespace fw
