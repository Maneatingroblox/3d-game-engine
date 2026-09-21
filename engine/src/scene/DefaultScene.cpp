#include "engine/scene/DefaultScene.h"
#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/brush/BrushMap.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>

// stb_image_write's implementation lives in engine/src/lightmap/Lightmapper.cpp
// (same library), so only the declarations are needed here.
#include <stb_image_write.h>

namespace fs = std::filesystem;

namespace fw {
namespace DefaultScene {

namespace {

Entity AddPrimitive(Scene& scene, const std::string& name, const std::string& meshAsset,
                    const vec3& position, const vec3& scale = vec3(1.0f),
                    const vec3& eulerDegrees = vec3(0.0f)) {
    Entity e = scene.CreateEntity(name);
    auto& tc = e.Get<TransformComponent>();
    tc.local.position = position;
    tc.local.scale = scale;
    if (eulerDegrees != vec3(0.0f)) tc.local.SetEulerDegrees(eulerDegrees);
    auto& mr = e.AddOrReplace<MeshRendererComponent>();
    mr.meshAsset = meshAsset;
    mr.materialSlots = { "assets/materials/dev_grey.fwmat" };
    return e;
}

// Yaw/pitch (degrees) that make a camera at `from` look at `target`, using the
// same convention as editor/src/EditorApp.cpp's fly camera and
// RenderTypes.h's YawPitchForward(): pitch about +X, yaw about +Y.
vec3 LookAtEulerDegrees(const vec3& from, const vec3& target) {
    const vec3 d = glm::normalize(target - from);
    // forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch))
    const float yaw = Degrees(std::atan2(-d.x, -d.z));
    const float pitch = Degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
    return vec3(pitch, yaw, 0.0f);
}

// Procedural "dev" texture for Hammer-mode brushes: a mid-grey surface with a
// subtle grid, so newly created blocks read as textured geometry instead of an
// untextured white blob (the default `BrushFace::material` points here).
void WriteDevTextureIfMissing(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return;
    fs::create_directories(fs::path(path).parent_path(), ec);

    const int size = 128;
    std::vector<unsigned char> pixels((size_t)size * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const int cellSize = 32;
            const bool line = (x % cellSize == 0) || (y % cellSize == 0);
            const bool checker = (((x / cellSize) + (y / cellSize)) % 2) == 0;
            const float base = checker ? 0.62f : 0.56f;
            const float value = line ? base * 0.72f : base;
            unsigned char* p = &pixels[((size_t)y * size + x) * 4];
            p[0] = (unsigned char)(value * 255.0f);
            p[1] = (unsigned char)(value * 255.0f);
            p[2] = (unsigned char)(value * 1.02f * 255.0f);
            p[3] = 255;
        }
    }
    if (stbi_write_png(path.c_str(), size, size, 4, pixels.data(), size * 4))
        FW_LOG_INFO("Created dev texture: %s", path.c_str());
}

void WriteFileIfMissing(const std::string& path, const std::string& contents) {
    std::error_code ec;
    if (fs::exists(path, ec)) return;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path);
    if (!out) {
        FW_LOG_WARN("Could not write '%s'", path.c_str());
        return;
    }
    out << contents;
    FW_LOG_INFO("Created starter asset: %s", path.c_str());
}

} // namespace

std::string DefaultScenePath() { return "assets/scenes/default.fwscene"; }

void Build(Scene& scene, BrushMap* brushMap) {
    scene.SetName("Default");

    // ---- sky ------------------------------------------------------------
    Entity sky = scene.CreateEntity("Sky");
    auto& skyLight = sky.AddOrReplace<SkyLightComponent>();
    skyLight.ambientColor = vec3(0.17f, 0.19f, 0.23f);
    skyLight.ambientIntensity = 1.0f;

    // ---- sun ------------------------------------------------------------
    Entity sun = scene.CreateEntity("Sun (Directional Light)");
    sun.Get<TransformComponent>().local.SetEulerDegrees(vec3(-42.0f, -35.0f, 0.0f));
    auto& sunLight = sun.AddOrReplace<LightComponent>();
    sunLight.type = LightType::Directional;
    sunLight.intensity = 2.6f;
    sunLight.color = vec3(1.0f, 0.96f, 0.9f);
    sunLight.castsShadows = true;

    // ---- ground ---------------------------------------------------------
    Entity ground = AddPrimitive(scene, "Ground", "builtin:plane", vec3(0.0f, -0.01f, 0.0f), vec3(24.0f, 1.0f, 24.0f));
    {
        auto& mr = ground.Get<MeshRendererComponent>();
        mr.materialSlots = { "assets/materials/dev_grey.fwmat" };
        mr.receiveShadows = true;
        mr.castShadows = false;
        ground.AddOrReplace<ColliderComponent>().shape = ColliderShape::Box;
        ground.Get<ColliderComponent>().halfExtents = vec3(12.0f, 0.01f, 12.0f);
        ground.AddOrReplace<RigidBodyComponent>().motionType = BodyMotionType::Static;
    }

    // ---- a few things to look at / move around --------------------------
    Entity box = AddPrimitive(scene, "Box (dynamic)", "builtin:cube", vec3(-2.0f, 0.5f, 0.0f));
    box.AddOrReplace<ColliderComponent>().shape = ColliderShape::Box;
    box.Get<ColliderComponent>().halfExtents = vec3(0.5f);
    box.AddOrReplace<RigidBodyComponent>().motionType = BodyMotionType::Dynamic;

    box.Get<MeshRendererComponent>().materialSlots = { "assets/materials/warm_red.fwmat" };

    Entity sphere = AddPrimitive(scene, "Sphere (dynamic)", "builtin:sphere", vec3(0.0f, 1.2f, 0.0f));
    sphere.AddOrReplace<ColliderComponent>().shape = ColliderShape::Sphere;
    sphere.Get<ColliderComponent>().radius = 0.5f;
    sphere.AddOrReplace<RigidBodyComponent>().motionType = BodyMotionType::Dynamic;

    Entity ramp = AddPrimitive(scene, "Ramp", "builtin:ramp", vec3(2.6f, 0.5f, 0.0f));
    ramp.AddOrReplace<ColliderComponent>().shape = ColliderShape::Box;
    ramp.Get<ColliderComponent>().halfExtents = vec3(0.5f);

    Entity cylinder = AddPrimitive(scene, "Cylinder", "builtin:cylinder", vec3(0.0f, 0.5f, -2.4f));
    cylinder.Get<MeshRendererComponent>().materialSlots = { "assets/materials/steel_blue.fwmat" };

    Entity pointLight = scene.CreateEntity("Point Light");
    pointLight.Get<TransformComponent>().local.position = vec3(3.0f, 3.0f, 2.0f);
    auto& pl = pointLight.AddOrReplace<LightComponent>();
    pl.type = LightType::Point;
    pl.color = vec3(1.0f, 0.78f, 0.5f);
    pl.intensity = 4.0f;
    pl.range = 12.0f;

    // ---- camera ---------------------------------------------------------
    // Positioned so the whole starter scene (ground + props) is in frame from
    // the moment the editor opens - "press Play and something visible happens".
    Entity camera = scene.CreateEntity("Main Camera");
    const vec3 camPos(5.0f, 2.9f, 6.4f);
    camera.Get<TransformComponent>().local.position = camPos;
    camera.Get<TransformComponent>().local.SetEulerDegrees(LookAtEulerDegrees(camPos, vec3(0.2f, 0.8f, -0.5f)));
    auto& cam = camera.AddOrReplace<CameraComponent>();
    cam.isPrimary = true;
    cam.fovDeg = 60.0f;
    cam.nearClip = 0.05f;
    cam.farClip = 2000.0f;

    // ---- matching Hammer-mode brushes ------------------------------------
    // The ground is also authored as a brush so Hammer mode has real geometry
    // to select, clip and carve on first use.
    if (brushMap) {
        Brush groundBrush = Brush::CreateBox(vec3(-20.0f, -1.0f, -20.0f), vec3(20.0f, 0.0f, 20.0f),
                                             "assets/textures/dev/dev_grey.png");
        groundBrush.Rebuild();
        groundBrush.classname = "worldspawn";
        brushMap->AddBrush(groundBrush);
        brushMap->MarkAllDirty();
    }

    scene.UpdateTransforms();
    FW_LOG_INFO("Built starter scene: %d entities",
                (int)scene.Registry().view<IDComponent>().size());
}

bool EnsureStarterContent(const std::string& scenePath) {
    const std::string resolvedScene = Paths::Resolve(scenePath);
    std::error_code ec;
    if (fs::exists(resolvedScene, ec)) return true;

    // Dev material used by the primitives (a mid-grey, slightly rough surface).
    WriteFileIfMissing(Paths::Resolve("assets/materials/dev_grey.fwmat"), R"({
  "albedoColor": [0.42, 0.43, 0.46, 1.0],
  "emissiveColor": [0.0, 0.0, 0.0],
  "emissiveStrength": 1.0,
  "metallic": 0.0,
  "roughness": 0.75,
  "albedoMap": "",
  "normalMap": "",
  "metallicRoughnessMap": "",
  "emissiveMap": "",
  "doubleSided": false,
  "transparent": false,
  "shaderAsset": "assets/shaders/Mesh.hlsl"
}
)");
    WriteFileIfMissing(Paths::Resolve("assets/materials/warm_red.fwmat"), R"({
  "albedoColor": [0.65, 0.22, 0.16, 1.0],
  "emissiveColor": [0.0, 0.0, 0.0],
  "emissiveStrength": 1.0,
  "metallic": 0.0,
  "roughness": 0.55,
  "shaderAsset": "assets/shaders/Mesh.hlsl"
}
)");
    WriteFileIfMissing(Paths::Resolve("assets/materials/steel_blue.fwmat"), R"({
  "albedoColor": [0.28, 0.42, 0.68, 1.0],
  "emissiveColor": [0.0, 0.0, 0.0],
  "emissiveStrength": 1.0,
  "metallic": 0.35,
  "roughness": 0.35,
  "shaderAsset": "assets/shaders/Mesh.hlsl"
}
)");
    WriteFileIfMissing(Paths::Resolve("assets/materials/checker.fwmat"), R"({
  "albedoColor": [0.9, 0.35, 0.2, 1.0],
  "emissiveColor": [0.0, 0.0, 0.0],
  "emissiveStrength": 1.0,
  "metallic": 0.0,
  "roughness": 0.6,
  "shaderAsset": "assets/shaders/Mesh.hlsl"
}
)");

    // A sample Lua script so the Script Editor / scripting docs have a real
    // example to open.
    WriteFileIfMissing(Paths::Resolve("assets/scripts/spin.lua"), R"(-- Sample gameplay script: spins the entity it is attached to.
-- Attach it from the Inspector (Script > Script Asset) to any entity.
local speed = 45.0

function OnStart(self)
    -- Properties set in the Inspector are available as self.Properties
    if self.Properties and self.Properties.speed then
        speed = tonumber(self.Properties.speed) or speed
    end
    Log("spin.lua started on " .. self.Name)
end

function OnUpdate(self, dt)
    local yaw = self.Transform.Rotation.y + speed * dt
    self.Transform.Rotation = { x = self.Transform.Rotation.x, y = yaw, z = self.Transform.Rotation.z }
end
)");

    // Textures used by Hammer-mode brushes / the default material.
    WriteDevTextureIfMissing(Paths::Resolve("assets/textures/dev/dev_grey.png"));

    // The scene itself.
    Scene scene("Default");
    BrushMap brushMap;
    Build(scene, &brushMap);
    if (!SceneSerializer::Save(scene, resolvedScene, &brushMap)) {
        FW_LOG_ERROR("Could not write starter scene '%s'", resolvedScene.c_str());
        return false;
    }
    // Bake nothing; just make sure the brushes referenced above exist on disk
    // so Hammer mode has compiled geometry immediately.
    FW_LOG_INFO("Created starter scene: %s", resolvedScene.c_str());
    return true;
}

} // namespace DefaultScene
} // namespace fw
