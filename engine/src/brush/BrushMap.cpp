#include "engine/brush/BrushMap.h"
#include "engine/scene/Scene.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace fw {

namespace {

// Hammer-mode brushes name a *texture* per face (valve-style), while the
// renderer binds a .fwmat material per submesh. This bridges the two: if the
// slot already points at a .fwmat it is used as-is; otherwise a tiny material
// file is generated next to the compiled brushes that references the texture as
// its albedo map. Without this, brush faces silently fell back to the default
// (untextured) material.
std::string MaterialSlotForBrushTexture(const std::string& slot, const std::string& generatedDir) {
    if (slot.empty()) return "assets/materials/dev_grey.fwmat";
    if (slot.size() > 6 && slot.compare(slot.size() - 6, 6, ".fwmat") == 0) return slot;

    const fs::path texturePath(slot);
    const std::string stem = texturePath.stem().string();
    if (stem.empty()) return "assets/materials/dev_grey.fwmat";

    const fs::path outDir = fs::path(generatedDir) / "materials";
    const fs::path materialPath = outDir / (stem + ".fwmat");
    std::error_code ec;
    if (!fs::exists(materialPath, ec)) {
        fs::create_directories(outDir, ec);
        std::ofstream out(materialPath);
        if (out) {
            const bool hasTexture = fs::exists(Paths::Resolve(slot), ec);
            nlohmann::json j;
            j["albedoColor"] = { 1.0, 1.0, 1.0, 1.0 };
            j["metallic"] = 0.0;
            j["roughness"] = 0.8;
            j["albedoMap"] = hasTexture ? slot : std::string();
            j["shaderAsset"] = "assets/shaders/Mesh.hlsl";
            out << j.dump(2);
        }
    }
    return materialPath.generic_string();
}

} // namespace

Brush& BrushMap::AddBrush(Brush brush) {
    m_Brushes.push_back(std::move(brush));
    m_Dirty.insert(m_Brushes.back().id);
    return m_Brushes.back();
}

void BrushMap::RemoveBrush(UUID id) {
    m_Brushes.erase(std::remove_if(m_Brushes.begin(), m_Brushes.end(),
        [&](const Brush& b) { return b.id == id; }), m_Brushes.end());
    m_Dirty.erase(id);
    m_BrushToEntity.erase((u64)id);
}

Brush* BrushMap::FindBrush(UUID id) {
    for (auto& b : m_Brushes) if (b.id == id) return &b;
    return nullptr;
}

void BrushMap::MarkAllDirty() {
    for (auto& b : m_Brushes) m_Dirty.insert(b.id);
}

void BrushMap::CompileToScene(Scene& scene, const std::string& generatedMeshDir, bool dirtyOnly) {
    fs::create_directories(generatedMeshDir);
    for (auto& brush : m_Brushes) {
        u64 key = (u64)brush.id;
        if (dirtyOnly && !m_Dirty.count(brush.id) && m_BrushToEntity.count(key)) continue;

        brush.Rebuild();
        MeshData mesh = brush.ToMeshData();
        std::string meshPath = generatedMeshDir + "/brush_" + std::to_string(key) + ".fwmesh";
        MeshData::SaveFWMesh(meshPath, mesh);

        Entity entity;
        auto it = m_BrushToEntity.find(key);
        if (it != m_BrushToEntity.end() && scene.Registry().valid(it->second)) {
            entity = Entity(it->second, &scene.Registry());
        } else {
            entity = scene.CreateEntityWithId(brush.id, brush.classname.empty() ? "Brush" : brush.classname);
            m_BrushToEntity[key] = entity.Handle();
        }

        auto& mr = entity.AddOrReplace<MeshRendererComponent>();
        mr.meshAsset = meshPath;
        mr.materialSlots.clear();
        for (const auto& slot : mesh.materialSlotNames)
            mr.materialSlots.push_back(MaterialSlotForBrushTexture(slot, generatedMeshDir));
        if (mr.materialSlots.empty()) mr.materialSlots.push_back("assets/materials/dev_grey.fwmat");

        auto& bc = entity.AddOrReplace<BrushComponent>();
        bc.brushId = brush.id;
        bc.isTrigger = brush.isTrigger;
        bc.classname = brush.classname;

        if (!entity.Has<RigidBodyComponent>()) {
            auto& rb = entity.AddOrReplace<RigidBodyComponent>();
            rb.motionType = BodyMotionType::Static;
            rb.isTrigger = brush.isTrigger;
        }
        auto& col = entity.AddOrReplace<ColliderComponent>();
        col.shape = ColliderShape::TriangleMesh;
        col.collisionMeshAsset = meshPath;

        m_Dirty.erase(brush.id);
    }
}

bool BrushMap::SaveToFile(const std::string& path) const {
    json j;
    j["version"] = 1;
    json brushArray = json::array();
    for (auto& b : m_Brushes) {
        json jb;
        jb["id"] = (u64)b.id;
        jb["isTrigger"] = b.isTrigger;
        jb["classname"] = b.classname;
        json faces = json::array();
        for (auto& f : b.faces) {
            json jf;
            jf["normal"] = { f.plane.normal.x, f.plane.normal.y, f.plane.normal.z };
            jf["d"] = f.plane.d;
            jf["material"] = f.material;
            jf["uAxis"] = { f.uAxis.x, f.uAxis.y, f.uAxis.z };
            jf["vAxis"] = { f.vAxis.x, f.vAxis.y, f.vAxis.z };
            jf["uvOffset"] = { f.uvOffset.x, f.uvOffset.y };
            jf["uvScale"] = { f.uvScale.x, f.uvScale.y };
            jf["uvRotationDeg"] = f.uvRotationDeg;
            faces.push_back(jf);
        }
        jb["faces"] = faces;
        brushArray.push_back(jb);
    }
    j["brushes"] = brushArray;
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return true;
}

bool BrushMap::LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    json j; f >> j;
    m_Brushes.clear();
    m_Dirty.clear();
    m_BrushToEntity.clear();
    for (auto& jb : j.value("brushes", json::array())) {
        Brush b;
        b.id = UUID((u64)jb.value("id", (u64)UUID()));
        b.isTrigger = jb.value("isTrigger", false);
        b.classname = jb.value("classname", "");
        for (auto& jf : jb.value("faces", json::array())) {
            BrushFace face;
            auto n = jf["normal"];
            face.plane.normal = vec3(n[0].get<float>(), n[1].get<float>(), n[2].get<float>());
            face.plane.d = jf.value("d", 0.0f);
            face.material = jf.value("material", std::string("assets/textures/dev/dev_grey.png"));
            auto ua = jf["uAxis"]; face.uAxis = vec3(ua[0].get<float>(), ua[1].get<float>(), ua[2].get<float>());
            auto va = jf["vAxis"]; face.vAxis = vec3(va[0].get<float>(), va[1].get<float>(), va[2].get<float>());
            auto uo = jf["uvOffset"]; face.uvOffset = vec2(uo[0].get<float>(), uo[1].get<float>());
            auto us = jf["uvScale"]; face.uvScale = vec2(us[0].get<float>(), us[1].get<float>());
            face.uvRotationDeg = jf.value("uvRotationDeg", 0.0f);
            b.faces.push_back(face);
        }
        b.Rebuild();
        m_Brushes.push_back(b);
        m_Dirty.insert(b.id);
    }
    return true;
}

} // namespace fw
