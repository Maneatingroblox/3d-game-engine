#pragma once
// Hammer-style convex brush geometry. A Brush is defined the classic
// Source/Quake way: a convex volume described as the intersection of
// half-spaces (planes), each carrying its own texture/material and UV
// parameters. Brushes are authored in "Hammer mode" of the editor
// (face manipulation, clipping, vertex editing) and compiled into a
// renderable+collidable MeshData, exactly like Hammer compiles brushes with
// vbsp. Unlike Hammer, brushes here are just another kind of entity in the
// same ECS scene, so scripts can be attached to them and the engine treats
// them identically to mesh entities at runtime.

#include "engine/core/Base.h"
#include "engine/core/UUID.h"
#include "engine/math/Math.h"
#include "engine/asset/MeshData.h"
#include <vector>
#include <string>

namespace fw {

// One planar face of a brush.
struct BrushFace {
    Plane plane;
    std::string material = "assets/textures/dev/dev_grey.png";
    // Texture-space basis (like Hammer's texture axes) so the user can
    // translate/rotate/scale the texture per-face independent of geometry.
    vec3 uAxis{1, 0, 0};
    vec3 vAxis{0, 0, -1};
    vec2 uvOffset{0.0f};
    vec2 uvScale{0.25f, 0.25f}; // world units per texture repeat
    float uvRotationDeg = 0.0f;
    u32 smoothingGroup = 0;

    // Filled in by Brush::Rebuild(): the polygon (in world space, CCW as seen
    // from outside) formed by clipping this face's plane against all others.
    std::vector<vec3> polygon;
};

// A single convex brush = intersection of N half-spaces (faces).
class Brush {
public:
    UUID id;
    std::vector<BrushFace> faces;
    bool isTrigger = false;
    std::string classname; // "" = plain world geometry; else Hammer-style point/solid entity class

    // Recomputes each face's polygon by clipping the (very large) face plane
    // against every other face's plane. Standard Quake/Source-style
    // brush->polygon derivation for convex brushes.
    void Rebuild();

    // Converts the current polygons into a renderable/collidable mesh
    // (triangulated fans per face, correct normals/UVs from the face basis).
    MeshData ToMeshData() const;

    AABB Bounds() const;

    // A brush is valid if it still encloses a non-degenerate volume: at
    // least 4 faces, and at least 4 of them retain a non-empty clipped
    // polygon (a tetrahedron-or-larger convex solid needs >=4 real faces;
    // some faces of the original definition may clip away to nothing after
    // Clip()/Subtract() without invalidating the resulting brush).
    bool IsValid() const {
        if (faces.size() < 4) return false;
        int nonEmpty = 0;
        for (auto& f : faces) if (f.polygon.size() >= 3) nonEmpty++;
        return nonEmpty >= 4;
    }

    // -------- Construction helpers (used by the editor's brush tools) -----
    static Brush CreateBox(const vec3& mins, const vec3& maxs, const std::string& material = "assets/textures/dev/dev_grey.png");
    static Brush CreateWedge(const vec3& mins, const vec3& maxs, const std::string& material = "assets/textures/dev/dev_grey.png");
    static Brush CreateCylinder(const vec3& center, float radius, float height, int sides, const std::string& material = "assets/textures/dev/dev_grey.png");

    // The Clip tool: splits this brush into up to two brushes (front/back of
    // the clip plane), mirroring Hammer's clipping tool. Returns false if the
    // plane doesn't intersect the brush.
    static bool Clip(const Brush& source, const Plane& clipPlane, Brush* outFront, Brush* outBack);

    // Constructive solid geometry: subtracts `cutter` from `target`, Hammer's
    // "Carve" tool. Since target/cutter are both convex, subtract is
    // implemented as clipping `target` against each face-plane of `cutter`
    // (keeping the *outside* pieces) which yields 0..faces.size() convex
    // fragments forming the boolean difference.
    static std::vector<Brush> Subtract(const Brush& target, const Brush& cutter);
};

} // namespace fw
