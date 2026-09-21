#include "engine/brush/Brush.h"
#include "engine/core/Log.h"
#include <algorithm>
#include <limits>

namespace fw {

// Builds a giant quad lying on `plane`, used as the starting polygon before
// clipping against all the other half-spaces (classic Quake brush->winding).
static std::vector<vec3> BasePolygonForPlane(const Plane& plane, float size = 65536.0f) {
    vec3 n = plane.normal;
    vec3 up = (std::abs(n.y) < 0.99f) ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 right = glm::normalize(glm::cross(up, n));
    up = glm::normalize(glm::cross(n, right));
    vec3 origin = n * -plane.d;
    std::vector<vec3> poly = {
        origin - right * size - up * size,
        origin + right * size - up * size,
        origin + right * size + up * size,
        origin - right * size + up * size,
    };
    return poly;
}

// Sutherland–Hodgman: clips polygon to the *inside* (side the normal points
// away from, i.e. SignedDistance <= 0) of `plane`.
static std::vector<vec3> ClipPolygonToPlane(const std::vector<vec3>& poly, const Plane& plane, float eps = 1e-5f) {
    if (poly.empty()) return {};
    std::vector<vec3> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const vec3& cur = poly[i];
        const vec3& next = poly[(i + 1) % n];
        float dCur = plane.SignedDistance(cur);
        float dNext = plane.SignedDistance(next);
        bool curIn = dCur <= eps;
        bool nextIn = dNext <= eps;
        if (curIn) out.push_back(cur);
        if (curIn != nextIn) {
            float t = dCur / (dCur - dNext);
            out.push_back(Lerp(cur, next, t));
        }
    }
    return out;
}

void Brush::Rebuild() {
    for (size_t i = 0; i < faces.size(); i++) {
        std::vector<vec3> poly = BasePolygonForPlane(faces[i].plane);
        for (size_t j = 0; j < faces.size() && !poly.empty(); j++) {
            if (i == j) continue;
            poly = ClipPolygonToPlane(poly, faces[j].plane);
        }
        faces[i].polygon = poly;
    }
}

AABB Brush::Bounds() const {
    AABB b;
    for (auto& f : faces) for (auto& p : f.polygon) b.Grow(p);
    return b;
}

MeshData Brush::ToMeshData() const {
    MeshData mesh;
    // Build one submesh-per-unique-material for correct multi-material brushes.
    std::vector<std::string> matNames;
    auto matIndexFor = [&](const std::string& mat) -> int {
        for (size_t i = 0; i < matNames.size(); i++) if (matNames[i] == mat) return (int)i;
        matNames.push_back(mat);
        return (int)matNames.size() - 1;
    };

    struct Bucket { std::vector<u32> indices; };
    std::vector<Bucket> buckets;

    for (auto& face : faces) {
        if (face.polygon.size() < 3) continue;
        int matIdx = matIndexFor(face.material);
        if ((int)buckets.size() <= matIdx) buckets.resize(matIdx + 1);

        u32 base = (u32)mesh.vertices.size();
        for (auto& p : face.polygon) {
            Vertex v;
            v.position = p;
            v.normal = face.plane.normal;
            v.tangent = glm::normalize(face.uAxis);
            float u = glm::dot(p, face.uAxis) / std::max(face.uvScale.x, 1e-4f) + face.uvOffset.x;
            float vcoord = glm::dot(p, face.vAxis) / std::max(face.uvScale.y, 1e-4f) + face.uvOffset.y;
            v.uv0 = vec2(u, vcoord);
            mesh.vertices.push_back(v);
        }
        // Fan triangulation (faces are convex polygons from plane clipping).
        for (size_t k = 1; k + 1 < face.polygon.size(); k++) {
            buckets[matIdx].indices.push_back(base);
            buckets[matIdx].indices.push_back(base + (u32)k);
            buckets[matIdx].indices.push_back(base + (u32)k + 1);
        }
    }

    for (size_t i = 0; i < buckets.size(); i++) {
        SubMesh sm;
        sm.indexStart = (u32)mesh.indices.size();
        sm.indexCount = (u32)buckets[i].indices.size();
        sm.materialIndex = (int)i;
        mesh.indices.insert(mesh.indices.end(), buckets[i].indices.begin(), buckets[i].indices.end());
        if (sm.indexCount > 0) mesh.subMeshes.push_back(sm);
    }
    mesh.materialSlotNames = matNames.empty() ? std::vector<std::string>{"default"} : matNames;
    mesh.EnforceWindingFromNormals();
    mesh.RecalculateTangents();
    mesh.RecalculateBounds();
    return mesh;
}

Brush Brush::CreateBox(const vec3& mins, const vec3& maxs, const std::string& material) {
    Brush b;
    b.id = UUID();
    struct PlaneDef { vec3 n; float d; vec3 u; vec3 v; };
    PlaneDef defs[6] = {
        { {1,0,0},  -maxs.x, {0,1,0}, {0,0,-1} },
        { {-1,0,0},  mins.x, {0,1,0}, {0,0,1}  },
        { {0,1,0},  -maxs.y, {1,0,0}, {0,0,-1} },
        { {0,-1,0},  mins.y, {1,0,0}, {0,0,1}  },
        { {0,0,1},  -maxs.z, {1,0,0}, {0,1,0}  },
        { {0,0,-1},  mins.z, {-1,0,0},{0,1,0}  },
    };
    for (auto& d : defs) {
        BrushFace f;
        f.plane = Plane{ d.n, d.d };
        f.material = material;
        f.uAxis = d.u; f.vAxis = d.v;
        b.faces.push_back(f);
    }
    b.Rebuild();
    return b;
}

Brush Brush::CreateWedge(const vec3& mins, const vec3& maxs, const std::string& material) {
    Brush b;
    b.id = UUID();
    // A right-triangle prism: box with the +X,+Y corner sliced by a diagonal plane.
    vec3 diag = glm::normalize(vec3(maxs.y - mins.y, maxs.x - mins.x, 0.0f));
    BrushFace bottom, back, left, front, slope;
    bottom.plane = Plane{ {0,-1,0}, mins.y };
    back.plane   = Plane{ {0,0,-1}, mins.z };
    front.plane  = Plane{ {0,0,1}, -maxs.z };
    left.plane   = Plane{ {-1,0,0}, mins.x };
    // Slope from (mins.x, mins.y) to (maxs.x, maxs.y) rising diagonally
    vec3 slopeNormal = glm::normalize(vec3(maxs.y - mins.y, -(maxs.x - mins.x), 0.0f));
    slope.plane = Plane::FromPointNormal(vec3(maxs.x, mins.y, 0.0f), slopeNormal);

    for (auto* f : { &bottom, &back, &front, &left, &slope }) {
        f->material = material; f->uAxis = {1,0,0}; f->vAxis = {0,0,-1};
    }
    b.faces = { bottom, back, front, left, slope };
    b.Rebuild();
    return b;
}

Brush Brush::CreateCylinder(const vec3& center, float radius, float height, int sides, const std::string& material) {
    Brush b;
    b.id = UUID();
    sides = std::max(sides, 3);
    BrushFace top, bottom;
    top.plane = Plane{ {0,1,0}, -(center.y + height * 0.5f) };
    bottom.plane = Plane{ {0,-1,0}, (center.y - height * 0.5f) };
    top.material = bottom.material = material;
    top.uAxis = bottom.uAxis = {1,0,0}; top.vAxis = bottom.vAxis = {0,0,1};
    b.faces.push_back(top);
    b.faces.push_back(bottom);
    for (int i = 0; i < sides; i++) {
        float theta = (float)i / sides * 2.0f * kPi;
        vec3 n = vec3(std::cos(theta), 0, std::sin(theta));
        BrushFace f;
        f.plane = Plane::FromPointNormal(center + n * radius, n);
        f.material = material;
        f.uAxis = glm::normalize(vec3(-n.z, 0, n.x));
        f.vAxis = {0,1,0};
        b.faces.push_back(f);
    }
    b.Rebuild();
    return b;
}

bool Brush::Clip(const Brush& source, const Plane& clipPlane, Brush* outFront, Brush* outBack) {
    // "Front" = the side the plane's normal points to (SignedDistance > 0),
    // matching Hammer's clip tool convention (keep both sides by default).
    Plane backPlane{ -clipPlane.normal, -clipPlane.d };

    bool anyFront = false, anyBack = false;
    for (auto& f : source.faces) {
        for (auto& p : f.polygon) {
            float d = clipPlane.SignedDistance(p);
            if (d > 1e-4f) anyFront = true;
            if (d < -1e-4f) anyBack = true;
        }
    }
    if (!anyFront || !anyBack) return false; // plane doesn't actually split the brush

    if (outFront) {
        *outFront = source;
        outFront->id = UUID();
        BrushFace capFace;
        capFace.plane = backPlane; // cap plane normal must face outward from the *front* piece
        capFace.material = source.faces.empty() ? "assets/textures/dev/dev_grey.png" : source.faces[0].material;
        capFace.uAxis = {1,0,0}; capFace.vAxis = {0,0,-1};
        outFront->faces.push_back(capFace);
        outFront->Rebuild();
    }
    if (outBack) {
        *outBack = source;
        outBack->id = UUID();
        BrushFace capFace;
        capFace.plane = clipPlane;
        capFace.material = source.faces.empty() ? "assets/textures/dev/dev_grey.png" : source.faces[0].material;
        capFace.uAxis = {1,0,0}; capFace.vAxis = {0,0,-1};
        outBack->faces.push_back(capFace);
        outBack->Rebuild();
    }
    return true;
}

std::vector<Brush> Brush::Subtract(const Brush& target, const Brush& cutter) {
    // Boolean difference target - cutter for convex brushes: for each face
    // plane of the cutter, clip `target` to keep only the piece OUTSIDE that
    // plane (capped by the plane), accumulating one output fragment per
    // cutter face; the remaining "inside all cutter planes" residue (which
    // would be the intersection, i.e. the part removed) is discarded.
    std::vector<Brush> result;
    Brush remaining = target;

    for (size_t i = 0; i < cutter.faces.size(); i++) {
        const Plane& cutPlane = cutter.faces[i].plane;

        // Does remaining actually straddle this plane? If remaining is
        // entirely outside already, nothing more to cut; if entirely inside,
        // the whole thing is consumed (fully inside the cutter -> no fragment
        // for this plane, continue clipping the rest with 'remaining' as-is
        // would be wrong, so we stop early only when remaining is empty).
        bool anyOutside = false, anyInside = false;
        for (auto& f : remaining.faces) for (auto& p : f.polygon) {
            float d = cutPlane.SignedDistance(p);
            if (d > 1e-4f) anyOutside = true;
            if (d < -1e-4f) anyInside = true;
        }
        if (!anyInside) {
            // remaining is fully outside the cutter already -> whole thing survives, done.
            result.push_back(remaining);
            remaining.faces.clear();
            break;
        }
        if (!anyOutside) {
            // remaining is fully inside the cutter on this plane's side; keep
            // clipping against subsequent planes to see if it pokes out elsewhere.
            continue;
        }

        Brush outsidePiece, insidePiece;
        Plane outsidePlaneForCap = cutPlane; // outside piece capped by the cutter plane itself
        if (Brush::Clip(remaining, outsidePlaneForCap, &outsidePiece, &insidePiece)) {
            if (outsidePiece.IsValid()) result.push_back(outsidePiece);
            remaining = insidePiece; // keep narrowing the "inside cutter" residue
        }
    }
    return result;
}

} // namespace fw
