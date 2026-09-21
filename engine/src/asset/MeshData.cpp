#include "engine/asset/MeshData.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <fstream>
#include <cstring>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <initializer_list>

namespace fw {

void MeshData::RecalculateBounds() {
    bounds = AABB();
    for (auto& v : vertices) bounds.Grow(v.position);
}

void MeshData::RecalculateNormals() {
    for (auto& v : vertices) v.normal = vec3(0.0f);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        u32 i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        vec3 e1 = vertices[i1].position - vertices[i0].position;
        vec3 e2 = vertices[i2].position - vertices[i0].position;
        vec3 n = glm::cross(e1, e2);
        vertices[i0].normal += n;
        vertices[i1].normal += n;
        vertices[i2].normal += n;
    }
    for (auto& v : vertices) {
        if (glm::length2(v.normal) > 1e-12f) v.normal = glm::normalize(v.normal);
        else v.normal = vec3(0, 1, 0);
    }
}

void MeshData::RecalculateTangents() {
    std::vector<vec3> tan(vertices.size(), vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        u32 i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex& v0 = vertices[i0]; const Vertex& v1 = vertices[i1]; const Vertex& v2 = vertices[i2];
        vec3 e1 = v1.position - v0.position;
        vec3 e2 = v2.position - v0.position;
        vec2 duv1 = v1.uv0 - v0.uv0;
        vec2 duv2 = v2.uv0 - v0.uv0;
        float denom = duv1.x * duv2.y - duv2.x * duv1.y;
        float f = std::abs(denom) > 1e-8f ? 1.0f / denom : 0.0f;
        vec3 t = f * (duv2.y * e1 - duv1.y * e2);
        tan[i0] += t; tan[i1] += t; tan[i2] += t;
    }
    for (size_t i = 0; i < vertices.size(); i++) {
        vec3 t = tan[i];
        if (glm::length2(t) < 1e-12f) { vertices[i].tangent = vec3(1, 0, 0); continue; }
        // Gram-Schmidt orthogonalize
        vec3 n = vertices[i].normal;
        t = glm::normalize(t - n * glm::dot(n, t));
        vertices[i].tangent = t;
    }
}

void MeshData::EnforceWindingFromNormals() {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        u32 i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
        vec3 e1 = vertices[i1].position - vertices[i0].position;
        vec3 e2 = vertices[i2].position - vertices[i0].position;
        vec3 faceN = glm::cross(e1, e2);
        if (glm::length2(faceN) < 1e-18f) continue; // degenerate: leave alone
        // Average vertex normal as the "which way is out" reference. If the
        // winding's geometric normal faces the other way, swap two indices to
        // flip the triangle (fixes inside-out primitives / invisible faces).
        const vec3 ref = vertices[i0].normal + vertices[i1].normal + vertices[i2].normal;
        if (glm::dot(faceN, ref) < 0.0f) std::swap(indices[i + 1], indices[i + 2]);
    }
}

void MeshData::GenerateLightmapUVs(int atlasResolution) {
    // A pragmatic (not optimal) lightmap unwrap: classify each triangle by
    // its dominant normal axis, project to 2D, then pack all triangles into
    // a square atlas grid, one cell per triangle-cluster. This avoids
    // pulling in a full unwrapper (xatlas) while still producing usable,
    // non-overlapping UV2 coordinates for baking.
    size_t triCount = indices.size() / 3;
    if (triCount == 0) return;

    int gridDim = (int)std::ceil(std::sqrt((double)triCount));
    float cell = 1.0f / (float)gridDim;
    float pad = cell * 0.08f;

    for (size_t t = 0; t < triCount; t++) {
        u32 i0 = indices[t * 3 + 0], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        vec3 p0 = vertices[i0].position, p1 = vertices[i1].position, p2 = vertices[i2].position;
        vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        vec3 an = glm::abs(n);

        vec2 uv0, uv1, uv2;
        if (an.x >= an.y && an.x >= an.z) { uv0 = {p0.y, p0.z}; uv1 = {p1.y, p1.z}; uv2 = {p2.y, p2.z}; }
        else if (an.y >= an.x && an.y >= an.z) { uv0 = {p0.x, p0.z}; uv1 = {p1.x, p1.z}; uv2 = {p2.x, p2.z}; }
        else { uv0 = {p0.x, p0.y}; uv1 = {p1.x, p1.y}; uv2 = {p2.x, p2.y}; }

        // Normalize this triangle's local UVs into [0,1]
        vec2 mn = glm::min(uv0, glm::min(uv1, uv2));
        vec2 mx = glm::max(uv0, glm::max(uv1, uv2));
        vec2 size = glm::max(mx - mn, vec2(1e-5f));
        float scale = 1.0f / std::max(size.x, size.y);

        int cx = (int)(t % gridDim);
        int cy = (int)(t / gridDim);
        vec2 cellOrigin(cx * cell + pad, cy * cell + pad);
        float innerSize = cell - 2.0f * pad;

        auto place = [&](vec2 uv) {
            vec2 local = (uv - mn) * scale;
            return cellOrigin + local * innerSize;
        };

        vertices[i0].uv1 = place(uv0);
        vertices[i1].uv1 = place(uv1);
        vertices[i2].uv1 = place(uv2);
    }
    FW_UNUSED(atlasResolution);
}

bool MeshData::LoadOBJ(const std::string& path, MeshData& out, std::string* outError) {
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        if (outError) *outError = reader.Error();
        return false;
    }
    if (!reader.Warning().empty()) FW_LOG_WARN("OBJ warning (%s): %s", path.c_str(), reader.Warning().c_str());

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& materials = reader.GetMaterials();

    out = MeshData();
    for (auto& m : materials) out.materialSlotNames.push_back(m.name.empty() ? "default" : m.name);
    if (out.materialSlotNames.empty()) out.materialSlotNames.push_back("default");

    std::unordered_map<u64, u32> uniqueVerts;
    auto keyFor = [](int v, int n, int t) -> u64 {
        return (u64)(u32)(v + 1) | ((u64)(u32)(n + 1) << 21) | ((u64)(u32)(t + 1) << 42);
    };

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        std::unordered_map<int, std::vector<u32>> perMaterialIndices;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            int matId = f < shape.mesh.material_ids.size() ? shape.mesh.material_ids[f] : -1;
            if (matId < 0) matId = 0;
            for (int v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                u64 key = keyFor(idx.vertex_index, idx.normal_index, idx.texcoord_index);
                auto it = uniqueVerts.find(key);
                u32 vi;
                if (it != uniqueVerts.end()) {
                    vi = it->second;
                } else {
                    Vertex vert;
                    vert.position = vec3(
                        attrib.vertices[3 * idx.vertex_index + 0],
                        attrib.vertices[3 * idx.vertex_index + 1],
                        attrib.vertices[3 * idx.vertex_index + 2]);
                    if (idx.normal_index >= 0) {
                        vert.normal = vec3(
                            attrib.normals[3 * idx.normal_index + 0],
                            attrib.normals[3 * idx.normal_index + 1],
                            attrib.normals[3 * idx.normal_index + 2]);
                    }
                    if (idx.texcoord_index >= 0) {
                        vert.uv0 = vec2(
                            attrib.texcoords[2 * idx.texcoord_index + 0],
                            attrib.texcoords[2 * idx.texcoord_index + 1]);
                    }
                    vi = (u32)out.vertices.size();
                    out.vertices.push_back(vert);
                    uniqueVerts[key] = vi;
                }
                perMaterialIndices[matId].push_back(vi);
            }
            indexOffset += fv;
        }
        for (auto& [matId, idxList] : perMaterialIndices) {
            SubMesh sm;
            sm.indexStart = (u32)out.indices.size();
            sm.indexCount = (u32)idxList.size();
            sm.materialIndex = matId;
            out.indices.insert(out.indices.end(), idxList.begin(), idxList.end());
            out.subMeshes.push_back(sm);
        }
    }

    bool hadNormals = !attrib.normals.empty();
    if (!hadNormals) out.RecalculateNormals();
    out.RecalculateTangents();
    out.RecalculateBounds();
    return true;
}

// ---------------------------------------------------------------------------
// .fwmesh: tiny custom binary format. Header + vertex array + index array +
// submesh table + material slot name table.
// ---------------------------------------------------------------------------
namespace {
    constexpr u32 kFWMeshMagic = 0x4853574D; // 'MWSH'
    constexpr u32 kFWMeshVersion = 1;
}

bool MeshData::SaveFWMesh(const std::string& path, const MeshData& mesh) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)&kFWMeshMagic, sizeof(u32));
    f.write((const char*)&kFWMeshVersion, sizeof(u32));

    u32 vcount = (u32)mesh.vertices.size();
    u32 icount = (u32)mesh.indices.size();
    u32 scount = (u32)mesh.subMeshes.size();
    u32 mcount = (u32)mesh.materialSlotNames.size();
    f.write((const char*)&vcount, sizeof(u32));
    f.write((const char*)&icount, sizeof(u32));
    f.write((const char*)&scount, sizeof(u32));
    f.write((const char*)&mcount, sizeof(u32));

    if (vcount) f.write((const char*)mesh.vertices.data(), sizeof(Vertex) * vcount);
    if (icount) f.write((const char*)mesh.indices.data(), sizeof(u32) * icount);
    if (scount) f.write((const char*)mesh.subMeshes.data(), sizeof(SubMesh) * scount);
    for (auto& name : mesh.materialSlotNames) {
        u32 len = (u32)name.size();
        f.write((const char*)&len, sizeof(u32));
        f.write(name.data(), len);
    }
    return true;
}

bool MeshData::LoadFWMesh(const std::string& path, MeshData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    u32 magic = 0, version = 0;
    f.read((char*)&magic, sizeof(u32));
    f.read((char*)&version, sizeof(u32));
    if (magic != kFWMeshMagic) return false;

    u32 vcount = 0, icount = 0, scount = 0, mcount = 0;
    f.read((char*)&vcount, sizeof(u32));
    f.read((char*)&icount, sizeof(u32));
    f.read((char*)&scount, sizeof(u32));
    f.read((char*)&mcount, sizeof(u32));

    out = MeshData();
    out.vertices.resize(vcount);
    out.indices.resize(icount);
    out.subMeshes.resize(scount);
    if (vcount) f.read((char*)out.vertices.data(), sizeof(Vertex) * vcount);
    if (icount) f.read((char*)out.indices.data(), sizeof(u32) * icount);
    if (scount) f.read((char*)out.subMeshes.data(), sizeof(SubMesh) * scount);
    out.materialSlotNames.resize(mcount);
    for (u32 i = 0; i < mcount; i++) {
        u32 len = 0;
        f.read((char*)&len, sizeof(u32));
        std::string s(len, '\0');
        f.read(s.data(), len);
        out.materialSlotNames[i] = s;
    }
    out.RecalculateBounds();
    return true;
}

// ---------------------------------------------------------------------------
// Primitive builders
// ---------------------------------------------------------------------------
MeshData MeshData::CreateBox(const vec3& he) {
    MeshData m;
    struct Face { vec3 n, u, v; };
    Face faces[6] = {
        {{1,0,0},{0,0,-1},{0,1,0}}, {{-1,0,0},{0,0,1},{0,1,0}},
        {{0,1,0},{1,0,0},{0,0,-1}}, {{0,-1,0},{1,0,0},{0,0,1}},
        {{0,0,1},{1,0,0},{0,1,0}}, {{0,0,-1},{-1,0,0},{0,1,0}},
    };
    for (auto& f : faces) {
        u32 base = (u32)m.vertices.size();
        vec3 center = f.n * he;
        vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};
        vec3 corners[4] = {
            center - f.u*he.x*0.f - f.v*he.y*0.f, {0,0,0},{0,0,0},{0,0,0}
        };
        // Build the 4 face corners properly using face-local axes scaled by half-extent components along u/v.
        vec3 uAxis = f.u, vAxis = f.v;
        float uHalf = glm::dot(glm::abs(uAxis), he);
        float vHalf = glm::dot(glm::abs(vAxis), he);
        corners[0] = center - uAxis*uHalf - vAxis*vHalf;
        corners[1] = center + uAxis*uHalf - vAxis*vHalf;
        corners[2] = center + uAxis*uHalf + vAxis*vHalf;
        corners[3] = center - uAxis*uHalf + vAxis*vHalf;
        for (int i = 0; i < 4; i++) {
            Vertex vert;
            vert.position = corners[i];
            vert.normal = f.n;
            vert.uv0 = uvs[i];
            vert.tangent = uAxis;
            m.vertices.push_back(vert);
        }
        m.indices.insert(m.indices.end(), { base, base+1, base+2, base, base+2, base+3 });
    }
    SubMesh sm; sm.indexStart = 0; sm.indexCount = (u32)m.indices.size(); sm.materialIndex = 0;
    m.subMeshes.push_back(sm);
    m.materialSlotNames.push_back("default");
    m.EnforceWindingFromNormals();
    m.RecalculateBounds();
    return m;
}

MeshData MeshData::CreateSphere(float radius, int segments) {
    MeshData m;
    int rings = segments / 2;
    for (int y = 0; y <= rings; y++) {
        float v = (float)y / rings;
        float phi = v * kPi;
        for (int x = 0; x <= segments; x++) {
            float u = (float)x / segments;
            float theta = u * 2.0f * kPi;
            vec3 dir(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = dir * radius;
            vert.normal = dir;
            vert.uv0 = vec2(u, v);
            vert.tangent = glm::normalize(vec3(-std::sin(theta), 0, std::cos(theta)));
            m.vertices.push_back(vert);
        }
    }
    // Winding: counter-clockwise seen from OUTSIDE, matching the convention
    // used by RecalculateNormals()/CreateBox(). (i0 = (y,x), i1 = (y+1,x),
    // i0+1 = (y,x+1), i1+1 = (y+1,x+1); increasing y goes down, increasing x
    // goes towards +Z at theta = 0, so (i0, i0+1, i1) faces +X i.e. outward.)
    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            u32 i0 = y * (segments + 1) + x;
            u32 i1 = i0 + segments + 1;
            // Skip the degenerate triangles at the poles (the whole ring there
            // shares one position - the triangles had zero area but produced
            // garbage normals/winding and shaded artefacts near the caps).
            const bool northPole = (y == 0);
            const bool southPole = (y == rings - 1);
            if (!northPole) m.indices.insert(m.indices.end(), { i0, i0+1, i1 });
            if (!southPole) m.indices.insert(m.indices.end(), { i0+1, i1+1, i1 });
        }
    }
    m.EnforceWindingFromNormals();
    SubMesh sm; sm.indexCount = (u32)m.indices.size();
    m.subMeshes.push_back(sm);
    m.materialSlotNames.push_back("default");
    m.RecalculateBounds();
    return m;
}

MeshData MeshData::CreatePlane(float sizeX, float sizeZ, int subdivisions) {
    MeshData m;
    int n = std::max(1, subdivisions);
    for (int z = 0; z <= n; z++) {
        for (int x = 0; x <= n; x++) {
            Vertex v;
            float fx = ((float)x / n - 0.5f) * sizeX;
            float fz = ((float)z / n - 0.5f) * sizeZ;
            v.position = vec3(fx, 0, fz);
            v.normal = vec3(0, 1, 0);
            v.tangent = vec3(1, 0, 0);
            v.uv0 = vec2((float)x / n, (float)z / n);
            m.vertices.push_back(v);
        }
    }
    for (int z = 0; z < n; z++) {
        for (int x = 0; x < n; x++) {
            u32 i0 = z * (n + 1) + x;
            u32 i1 = i0 + 1;
            u32 i2 = i0 + (n + 1);
            u32 i3 = i2 + 1;
            m.indices.insert(m.indices.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }
    SubMesh sm; sm.indexCount = (u32)m.indices.size();
    m.subMeshes.push_back(sm);
    m.materialSlotNames.push_back("default");
    m.EnforceWindingFromNormals();
    m.RecalculateBounds();
    return m;
}

MeshData MeshData::CreateCylinder(float radius, float height, int segments) {
    MeshData m;
    float halfH = height * 0.5f;
    // Side
    for (int i = 0; i <= segments; i++) {
        float t = (float)i / segments;
        float theta = t * 2.0f * kPi;
        vec3 dir(std::cos(theta), 0, std::sin(theta));
        Vertex top, bot;
        top.position = dir * radius + vec3(0, halfH, 0);
        bot.position = dir * radius - vec3(0, halfH, 0);
        top.normal = bot.normal = dir;
        top.uv0 = vec2(t, 0); bot.uv0 = vec2(t, 1);
        m.vertices.push_back(top);
        m.vertices.push_back(bot);
    }
    for (int i = 0; i < segments; i++) {
        u32 i0 = i * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
        m.indices.insert(m.indices.end(), { i0, i2, i1, i1, i2, i3 });
    }
    // Caps
    u32 topCenter = (u32)m.vertices.size();
    { Vertex v; v.position = {0, halfH, 0}; v.normal = {0,1,0}; v.uv0 = {0.5f,0.5f}; m.vertices.push_back(v); }
    u32 botCenter = (u32)m.vertices.size();
    { Vertex v; v.position = {0, -halfH, 0}; v.normal = {0,-1,0}; v.uv0 = {0.5f,0.5f}; m.vertices.push_back(v); }
    u32 ringStart = (u32)m.vertices.size();
    for (int i = 0; i <= segments; i++) {
        float t = (float)i / segments;
        float theta = t * 2.0f * kPi;
        vec3 dir(std::cos(theta), 0, std::sin(theta));
        Vertex top, bot;
        top.position = dir * radius + vec3(0, halfH, 0); top.normal = {0,1,0}; top.uv0 = vec2(dir.x,dir.z)*0.5f+0.5f;
        bot.position = dir * radius - vec3(0, halfH, 0); bot.normal = {0,-1,0}; bot.uv0 = vec2(dir.x,dir.z)*0.5f+0.5f;
        m.vertices.push_back(top);
        m.vertices.push_back(bot);
    }
    // Caps are wound counter-clockwise as seen from outside as well (the top
    // cap faces +Y, so its triangles must run clockwise when viewed from
    // above in the (theta increasing towards +Z) parameterisation).
    for (int i = 0; i < segments; i++) {
        u32 t0 = ringStart + i * 2, t1 = ringStart + (i + 1) * 2;
        u32 b0 = t0 + 1, b1 = t1 + 1;
        m.indices.insert(m.indices.end(), { topCenter, t1, t0 });
        m.indices.insert(m.indices.end(), { botCenter, b0, b1 });
    }

    SubMesh sm; sm.indexCount = (u32)m.indices.size();
    m.subMeshes.push_back(sm);
    m.materialSlotNames.push_back("default");
    m.EnforceWindingFromNormals();
    m.RecalculateTangents();
    m.RecalculateBounds();
    return m;
}

// ---------------------------------------------------------------------------
// Built-in primitive assets ("builtin:cube", ...).
//
// MeshRendererComponent::meshAsset may name one of these instead of a file,
// which is what lets the editor place geometry (and the starter scene contain
// something to look at) on a fresh clone where no model files exist yet.
// ---------------------------------------------------------------------------
namespace {
const char* kBuiltinNames[] = { "builtin:cube", "builtin:sphere", "builtin:plane", "builtin:cylinder",
                                "builtin:ramp", nullptr };
} // namespace

const char* const* MeshData::BuiltinPrimitiveNames() { return kBuiltinNames; }

bool MeshData::IsBuiltinPrimitive(const std::string& assetPath) {
    return assetPath.rfind("builtin:", 0) == 0;
}

bool MeshData::CreateBuiltin(const std::string& assetPath, MeshData& out) {
    const std::string name = assetPath.rfind("builtin:", 0) == 0 ? assetPath.substr(8) : assetPath;
    if (name == "cube" || name == "box") {
        out = CreateBox(vec3(0.5f));
    } else if (name == "sphere") {
        out = CreateSphere(0.5f, 32);
    } else if (name == "plane") {
        out = CreatePlane(1.0f, 1.0f, 1);
    } else if (name == "cylinder") {
        out = CreateCylinder(0.5f, 1.0f, 32);
    } else if (name == "ramp") {
        // A wedge - handy for testing slopes / character-controller step-up.
        // Faces are authored counter-clockwise as seen from outside (same
        // convention as CreateBox), and each face gets its own vertices so
        // normals stay flat.
        out = MeshData();
        const vec3 A(-0.5f, -0.5f, 0.5f), B(0.5f, -0.5f, 0.5f), C(0.5f, -0.5f, -0.5f), D(-0.5f, -0.5f, -0.5f);
        const vec3 E(-0.5f, 0.5f, -0.5f), F(0.5f, 0.5f, -0.5f);

        auto addFace = [&out](std::initializer_list<vec3> points) {
            std::vector<vec3> pts(points);
            if (pts.size() < 3) return;
            const vec3 n = glm::normalize(glm::cross(pts[1] - pts[0], pts[2] - pts[0]));
            const u32 base = (u32)out.vertices.size();
            for (size_t i = 0; i < pts.size(); i++) {
                Vertex v;
                v.position = pts[i];
                v.normal = n;
                v.uv0 = vec2((i == 1 || i == 2) ? 1.0f : 0.0f, (i >= 2) ? 1.0f : 0.0f);
                out.vertices.push_back(v);
            }
            // Triangle fan over the polygon (all faces here are convex).
            for (u32 i = 1; i + 1 < (u32)pts.size(); i++) {
                out.indices.insert(out.indices.end(), { base, base + i, base + i + 1 });
            }
        };

        addFace({ A, D, C, B }); // bottom (-Y)
        addFace({ A, B, F, E }); // slope (+Y/+Z)
        addFace({ D, E, F, C }); // back  (-Z)
        addFace({ A, E, D });    // left  (-X)
        addFace({ B, C, F });    // right (+X)

        out.EnforceWindingFromNormals();
        out.RecalculateTangents();
        SubMesh sm; sm.indexCount = (u32)out.indices.size();
        out.subMeshes.push_back(sm);
        out.materialSlotNames.push_back("default");
        out.RecalculateBounds();
    } else {
        FW_LOG_WARN("Unknown built-in primitive: %s", assetPath.c_str());
        return false;
    }

    if (out.subMeshes.empty()) {
        SubMesh sm; sm.indexCount = (u32)out.indices.size();
        out.subMeshes.push_back(sm);
    }
    out.RecalculateBounds();
    return true;
}

bool MeshData::LoadAny(const std::string& assetPath, MeshData& out, std::string* outError) {
    if (assetPath.empty()) {
        if (outError) *outError = "empty mesh asset path";
        return false;
    }
    if (IsBuiltinPrimitive(assetPath)) return CreateBuiltin(assetPath, out);

    const std::string resolved = Paths::Resolve(assetPath);
    std::string ext = std::filesystem::path(resolved).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    bool ok = false;
    if (ext == ".fwmesh") {
        ok = LoadFWMesh(resolved, out);
        if (!ok && outError) *outError = "failed to read .fwmesh (file missing or corrupt)";
    } else {
        ok = LoadOBJ(resolved, out, outError);
    }
    return ok;
}

} // namespace fw
