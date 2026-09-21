#include "engine/scene/SceneSerializer.h"
#include "engine/scene/Scene.h"
#include "engine/brush/BrushMap.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace fw {

static json ToJson(const vec3& v) { return json{ v.x, v.y, v.z }; }
static json ToJson(const vec2& v) { return json{ v.x, v.y }; }
static json ToJson(const quat& q) { return json{ q.x, q.y, q.z, q.w }; }
static vec3 Vec3From(const json& j) { return vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); }
static vec2 Vec2From(const json& j) { return vec2(j[0].get<float>(), j[1].get<float>()); }
static quat QuatFrom(const json& j) { return quat(j[3].get<float>(), j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); }

bool SceneSerializer::Save(const Scene& sceneConst, const std::string& rawPath, const BrushMap* brushMap) {
    Scene& scene = const_cast<Scene&>(sceneConst);
    const std::string path = Paths::Resolve(rawPath);
    json root;
    root["version"] = 1;
    root["name"] = scene.Name();
    json entities = json::array();

    scene.Each<IDComponent, NameComponent, TransformComponent, HierarchyComponent>(
        [&](Entity e, IDComponent& id, NameComponent& nc, TransformComponent& tc, HierarchyComponent& h) {
            json je;
            je["id"] = (u64)id.id;
            je["name"] = nc.name;
            je["parent"] = (h.parent != entt::null && scene.Registry().valid(h.parent))
                ? (u64)scene.Registry().get<IDComponent>(h.parent).id : 0;
            je["position"] = ToJson(tc.local.position);
            je["rotation"] = ToJson(tc.local.rotation);
            je["scale"] = ToJson(tc.local.scale);

            if (auto* c = e.TryGet<MeshRendererComponent>()) {
                json j;
                j["meshAsset"] = c->meshAsset;
                j["materialSlots"] = c->materialSlots;
                j["castShadows"] = c->castShadows;
                j["receiveShadows"] = c->receiveShadows;
                j["useLightmap"] = c->useLightmap;
                j["lightmapAsset"] = c->lightmapAsset;
                je["MeshRenderer"] = j;
            }
            if (auto* c = e.TryGet<LightComponent>()) {
                json j;
                j["type"] = (int)c->type;
                j["color"] = ToJson(c->color);
                j["intensity"] = c->intensity;
                j["range"] = c->range;
                j["innerConeDeg"] = c->innerConeDeg;
                j["outerConeDeg"] = c->outerConeDeg;
                j["castsShadows"] = c->castsShadows;
                j["isStatic"] = c->isStatic;
                je["Light"] = j;
            }
            if (auto* c = e.TryGet<CameraComponent>()) {
                json j;
                j["isPrimary"] = c->isPrimary;
                j["fovDeg"] = c->fovDeg;
                j["nearClip"] = c->nearClip;
                j["farClip"] = c->farClip;
                j["orthographic"] = c->orthographic;
                j["orthoSize"] = c->orthoSize;
                je["Camera"] = j;
            }
            if (auto* c = e.TryGet<SkyLightComponent>()) {
                json j;
                j["ambientColor"] = ToJson(c->ambientColor);
                j["ambientIntensity"] = c->ambientIntensity;
                j["skyboxAsset"] = c->skyboxAsset;
                je["SkyLight"] = j;
            }
            if (auto* c = e.TryGet<RigidBodyComponent>()) {
                json j;
                j["motionType"] = (int)c->motionType;
                j["mass"] = c->mass;
                j["friction"] = c->friction;
                j["restitution"] = c->restitution;
                j["linearDamping"] = c->linearDamping;
                j["angularDamping"] = c->angularDamping;
                j["isTrigger"] = c->isTrigger;
                j["gravityEnabled"] = c->gravityEnabled;
                je["RigidBody"] = j;
            }
            if (auto* c = e.TryGet<ColliderComponent>()) {
                json j;
                j["shape"] = (int)c->shape;
                j["halfExtents"] = ToJson(c->halfExtents);
                j["radius"] = c->radius;
                j["height"] = c->height;
                j["offset"] = ToJson(c->offset);
                j["collisionMeshAsset"] = c->collisionMeshAsset;
                je["Collider"] = j;
            }
            if (auto* c = e.TryGet<CharacterControllerComponent>()) {
                json j;
                j["radius"] = c->radius;
                j["height"] = c->height;
                j["maxSlopeDeg"] = c->maxSlopeDeg;
                j["stepHeight"] = c->stepHeight;
                je["CharacterController"] = j;
            }
            if (auto* c = e.TryGet<AudioSourceComponent>()) {
                json j;
                j["soundAsset"] = c->soundAsset;
                j["is3D"] = c->is3D;
                j["loop"] = c->loop;
                j["playOnStart"] = c->playOnStart;
                j["volume"] = c->volume;
                j["pitch"] = c->pitch;
                j["minDistance"] = c->minDistance;
                j["maxDistance"] = c->maxDistance;
                je["AudioSource"] = j;
            }
            if (e.Has<AudioListenerComponent>()) je["AudioListener"] = json{{"isPrimary", e.Get<AudioListenerComponent>().isPrimary}};
            if (auto* c = e.TryGet<ScriptComponent>()) {
                json j;
                j["scriptAsset"] = c->scriptAsset;
                j["enabled"] = c->enabled;
                j["properties"] = c->properties;
                je["Script"] = j;
            }
            if (auto* c = e.TryGet<BrushComponent>()) {
                json j;
                j["brushId"] = (u64)c->brushId;
                j["isTrigger"] = c->isTrigger;
                j["classname"] = c->classname;
                je["Brush"] = j;
            }
            entities.push_back(je);
        });

    root["entities"] = entities;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (!f) return false;
    f << root.dump(2);

    if (brushMap) {
        std::string brushPath = path + ".brushes.json";
        brushMap->SaveToFile(brushPath);
    }
    FW_LOG_INFO("Scene saved: %s (%zu entities)", path.c_str(), entities.size());
    return true;
}

bool SceneSerializer::Load(Scene& scene, const std::string& rawPath, BrushMap* brushMap) {
    const std::string path = Paths::Resolve(rawPath);
    std::ifstream f(path);
    if (!f) { FW_LOG_ERROR("Failed to open scene file: %s", path.c_str()); return false; }
    json root; f >> root;

    scene.SetName(root.value("name", std::string("Untitled")));

    // First pass: create all entities with their transforms.
    std::unordered_map<u64, u64> parentMap;
    for (auto& je : root.value("entities", json::array())) {
        u64 id = je.value("id", (u64)0);
        Entity e = scene.CreateEntityWithId(UUID(id), je.value("name", std::string("Entity")));
        auto& tc = e.Get<TransformComponent>();
        if (je.contains("position")) tc.local.position = Vec3From(je["position"]);
        if (je.contains("rotation")) tc.local.rotation = QuatFrom(je["rotation"]);
        if (je.contains("scale")) tc.local.scale = Vec3From(je["scale"]);

        u64 parentId = je.value("parent", (u64)0);
        if (parentId != 0) parentMap[id] = parentId;

        if (je.contains("MeshRenderer")) {
            auto& j = je["MeshRenderer"];
            auto& c = e.AddOrReplace<MeshRendererComponent>();
            c.meshAsset = j.value("meshAsset", std::string());
            c.materialSlots = j.value("materialSlots", std::vector<std::string>());
            c.castShadows = j.value("castShadows", true);
            c.receiveShadows = j.value("receiveShadows", true);
            c.useLightmap = j.value("useLightmap", false);
            c.lightmapAsset = j.value("lightmapAsset", std::string());
        }
        if (je.contains("Light")) {
            auto& j = je["Light"];
            auto& c = e.AddOrReplace<LightComponent>();
            c.type = (LightType)j.value("type", 1);
            c.color = j.contains("color") ? Vec3From(j["color"]) : vec3(1.0f);
            c.intensity = j.value("intensity", 1.0f);
            c.range = j.value("range", 10.0f);
            c.innerConeDeg = j.value("innerConeDeg", 25.0f);
            c.outerConeDeg = j.value("outerConeDeg", 35.0f);
            c.castsShadows = j.value("castsShadows", true);
            c.isStatic = j.value("isStatic", false);
        }
        if (je.contains("Camera")) {
            auto& j = je["Camera"];
            auto& c = e.AddOrReplace<CameraComponent>();
            c.isPrimary = j.value("isPrimary", true);
            c.fovDeg = j.value("fovDeg", 60.0f);
            c.nearClip = j.value("nearClip", 0.05f);
            c.farClip = j.value("farClip", 2000.0f);
            c.orthographic = j.value("orthographic", false);
            c.orthoSize = j.value("orthoSize", 10.0f);
        }
        if (je.contains("SkyLight")) {
            auto& j = je["SkyLight"];
            auto& c = e.AddOrReplace<SkyLightComponent>();
            c.ambientColor = j.contains("ambientColor") ? Vec3From(j["ambientColor"]) : vec3(0.15f);
            c.ambientIntensity = j.value("ambientIntensity", 1.0f);
            c.skyboxAsset = j.value("skyboxAsset", std::string());
        }
        if (je.contains("RigidBody")) {
            auto& j = je["RigidBody"];
            auto& c = e.AddOrReplace<RigidBodyComponent>();
            c.motionType = (BodyMotionType)j.value("motionType", 0);
            c.mass = j.value("mass", 1.0f);
            c.friction = j.value("friction", 0.6f);
            c.restitution = j.value("restitution", 0.1f);
            c.linearDamping = j.value("linearDamping", 0.05f);
            c.angularDamping = j.value("angularDamping", 0.05f);
            c.isTrigger = j.value("isTrigger", false);
            c.gravityEnabled = j.value("gravityEnabled", true);
        }
        if (je.contains("Collider")) {
            auto& j = je["Collider"];
            auto& c = e.AddOrReplace<ColliderComponent>();
            c.shape = (ColliderShape)j.value("shape", 0);
            c.halfExtents = j.contains("halfExtents") ? Vec3From(j["halfExtents"]) : vec3(0.5f);
            c.radius = j.value("radius", 0.5f);
            c.height = j.value("height", 1.0f);
            c.offset = j.contains("offset") ? Vec3From(j["offset"]) : vec3(0.0f);
            c.collisionMeshAsset = j.value("collisionMeshAsset", std::string());
        }
        if (je.contains("CharacterController")) {
            auto& j = je["CharacterController"];
            auto& c = e.AddOrReplace<CharacterControllerComponent>();
            c.radius = j.value("radius", 0.4f);
            c.height = j.value("height", 1.8f);
            c.maxSlopeDeg = j.value("maxSlopeDeg", 50.0f);
            c.stepHeight = j.value("stepHeight", 0.3f);
        }
        if (je.contains("AudioSource")) {
            auto& j = je["AudioSource"];
            auto& c = e.AddOrReplace<AudioSourceComponent>();
            c.soundAsset = j.value("soundAsset", std::string());
            c.is3D = j.value("is3D", true);
            c.loop = j.value("loop", false);
            c.playOnStart = j.value("playOnStart", false);
            c.volume = j.value("volume", 1.0f);
            c.pitch = j.value("pitch", 1.0f);
            c.minDistance = j.value("minDistance", 1.0f);
            c.maxDistance = j.value("maxDistance", 50.0f);
        }
        if (je.contains("AudioListener")) {
            e.AddOrReplace<AudioListenerComponent>().isPrimary = je["AudioListener"].value("isPrimary", true);
        }
        if (je.contains("Script")) {
            auto& j = je["Script"];
            auto& c = e.AddOrReplace<ScriptComponent>();
            c.scriptAsset = j.value("scriptAsset", std::string());
            c.enabled = j.value("enabled", true);
            c.properties = j.value("properties", std::unordered_map<std::string, std::string>());
        }
        if (je.contains("Brush")) {
            auto& j = je["Brush"];
            auto& c = e.AddOrReplace<BrushComponent>();
            c.brushId = UUID(j.value("brushId", (u64)0));
            c.isTrigger = j.value("isTrigger", false);
            c.classname = j.value("classname", std::string());
        }
    }

    // Second pass: wire up parenting now that all entities exist.
    for (auto& [childId, parentId] : parentMap) {
        Entity child = scene.FindByUUID(UUID(childId));
        Entity parent = scene.FindByUUID(UUID(parentId));
        if (child && parent) scene.SetParent(child, parent);
    }

    scene.UpdateTransforms();

    if (brushMap) {
        std::string brushPath = path + ".brushes.json";
        if (std::filesystem::exists(brushPath)) brushMap->LoadFromFile(brushPath);
    }

    FW_LOG_INFO("Scene loaded: %s", path.c_str());
    return true;
}

} // namespace fw
