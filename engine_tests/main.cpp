// Minimal smoke tests for the platform-independent engine core. This is not
// a full test suite (see docs/ for how these validate logic without a
// Windows/D3D11 host) - it exercises ECS, scene serialization, physics,
// scripting and brush/CSG so regressions in the core simulation are caught
// on any platform.
#include "engine/core/Engine.h"
#include "engine/core/Log.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/brush/Brush.h"
#include "engine/lightmap/Lightmapper.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace fw;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); g_failures++; } } while(0)

static void TestECSAndTransforms() {
    Scene scene("Test");
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent);

    parent.Get<TransformComponent>().local.position = vec3(10, 0, 0);
    child.Get<TransformComponent>().local.position = vec3(1, 0, 0);
    scene.UpdateTransforms();

    vec3 childWorldPos = vec3(child.Get<TransformComponent>().worldMatrix[3]);
    CHECK(glm::distance(childWorldPos, vec3(11, 0, 0)) < 1e-4f);

    scene.DestroyEntity(parent);
    CHECK(!child.IsValid()); // child destroyed too (cascading)
}

static void TestSceneSerialization() {
    Scene scene("SerTest");
    Entity e = scene.CreateEntity("Box");
    e.Get<TransformComponent>().local.position = vec3(1, 2, 3);
    auto& mr = e.AddOrReplace<MeshRendererComponent>();
    mr.meshAsset = "assets/models/box.fwmesh";
    auto& rb = e.AddOrReplace<RigidBodyComponent>();
    rb.motionType = BodyMotionType::Dynamic;
    auto& col = e.AddOrReplace<ColliderComponent>();
    col.shape = ColliderShape::Box;
    auto& script = e.AddOrReplace<ScriptComponent>();
    script.scriptAsset = "assets/scripts/test.lua";
    script.properties["speed"] = "5.0";

    std::filesystem::create_directories("test_output");
    CHECK(SceneSerializer::Save(scene, "test_output/test.fwscene"));

    Scene loaded;
    CHECK(SceneSerializer::Load(loaded, "test_output/test.fwscene"));
    Entity loadedBox = loaded.FindByName("Box");
    CHECK((bool)loadedBox);
    if (loadedBox) {
        CHECK(glm::distance(loadedBox.Get<TransformComponent>().local.position, vec3(1, 2, 3)) < 1e-4f);
        CHECK(loadedBox.Has<MeshRendererComponent>());
        CHECK(loadedBox.Has<RigidBodyComponent>());
        CHECK(loadedBox.Has<ScriptComponent>());
        CHECK(loadedBox.Get<ScriptComponent>().properties.at("speed") == "5.0");
    }
}

static void TestPhysicsFreeFall() {
    PhysicsWorld world;
    world.Init();
    world.SetGravity(vec3(0, -10, 0));

    BodyCreateInfo ground;
    ground.shape = BodyCreateInfo::Shape::Box;
    ground.halfExtents = vec3(50, 0.5f, 50);
    ground.isStatic = true;
    ground.position = vec3(0, -0.5f, 0);
    u32 groundId = world.CreateBody(ground);
    CHECK(groundId != 0xFFFFFFFF);

    BodyCreateInfo box;
    box.shape = BodyCreateInfo::Shape::Box;
    box.halfExtents = vec3(0.5f);
    box.isStatic = false;
    box.mass = 1.0f;
    box.position = vec3(0, 5, 0);
    u32 boxId = world.CreateBody(box);
    CHECK(boxId != 0xFFFFFFFF);

    for (int i = 0; i < 300; i++) world.Step(1.0f / 60.0f);

    vec3 pos; quat rot;
    world.GetBodyTransform(boxId, pos, rot);
    std::printf("Box settled at y=%.3f (expect ~0.5, resting on ground top at y=0)\n", pos.y);
    CHECK(pos.y > 0.4f && pos.y < 0.7f); // ground top at y=0 (pos -0.5 + halfExtent 0.5), box halfExtent 0.5

    RaycastHit hit = world.Raycast(vec3(0, 10, 0), vec3(0, -1, 0), 20.0f);
    CHECK(hit.hit);
    world.Shutdown();
}

static void TestScripting() {
    Scene scene("ScriptTest");
    PhysicsWorld world; world.Init();
    ScriptEngine scripting;
    scripting.Init(&scene, &world, nullptr);

    Entity e = scene.CreateEntity("Scripted");
    auto& sc = e.AddOrReplace<ScriptComponent>();

    std::filesystem::create_directories("test_output");
    {
        std::ofstream f("test_output/mover.lua");
        f << R"lua(
Speed = 2.0
function OnStart(self)
    self.entity:set_position(Vector3.new(0,0,0))
    Log.Info("script started")
end
function OnUpdate(self, dt)
    local p = self.entity:get_position()
    self.entity:set_position(p + Vector3.new(Speed * dt, 0, 0))
end
)lua";
    }
    sc.scriptAsset = "test_output/mover.lua";
    scripting.StartAll();
    for (int i = 0; i < 60; i++) scripting.Update(1.0f / 60.0f);

    vec3 pos = e.Get<TransformComponent>().local.position;
    std::printf("Scripted entity moved to x=%.3f (expect ~2.0)\n", pos.x);
    CHECK(std::abs(pos.x - 2.0f) < 0.2f);
    scripting.StopAll();
    world.Shutdown();
}

static void TestBrushCSG() {
    Brush box = Brush::CreateBox(vec3(-1), vec3(1));
    CHECK(box.IsValid());
    MeshData mesh = box.ToMeshData();
    CHECK(mesh.vertices.size() > 0);
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(std::abs(mesh.bounds.Extents().x - 1.0f) < 1e-3f);

    // Clip the box in half along X=0
    Brush front, back;
    Plane clipPlane = Plane::FromPointNormal(vec3(0,0,0), vec3(1,0,0));
    bool clipped = Brush::Clip(box, clipPlane, &front, &back);
    CHECK(clipped);
    if (clipped) {
        CHECK(front.IsValid());
        CHECK(back.IsValid());
        AABB fb = front.Bounds();
        AABB bb = back.Bounds();
        std::printf("front bounds: [%.2f,%.2f] back bounds: [%.2f,%.2f]\n", fb.min.x, fb.max.x, bb.min.x, bb.max.x);
        CHECK(fb.min.x > -0.01f);
        CHECK(bb.max.x < 0.01f);
    }

    // Subtract a smaller box from a bigger box
    Brush big = Brush::CreateBox(vec3(-2), vec3(2));
    Brush small = Brush::CreateBox(vec3(-0.5f), vec3(0.5f));
    auto pieces = Brush::Subtract(big, small);
    CHECK(!pieces.empty());
    std::printf("Subtract produced %zu fragment(s)\n", pieces.size());
}

static void TestLightmapBake() {
    MeshData plane = MeshData::CreatePlane(4.0f, 4.0f, 2);
    plane.GenerateLightmapUVs(64);

    std::vector<BakeOccluderTriangle> occluders; // no occluders, open sky
    std::vector<BakeStaticLight> lights;
    BakeStaticLight sun;
    sun.type = LightType::Directional;
    sun.direction = glm::normalize(vec3(0.3f, -1.0f, 0.2f));
    sun.color = vec3(1.0f, 0.95f, 0.85f);
    sun.intensity = 3.0f;
    lights.push_back(sun);

    LightmapBakeSettings settings;
    settings.resolution = 64;
    settings.samplesPerTexel = 8;

    LightmapResult result = Lightmapper::Bake(plane, mat4(1.0f), occluders, lights, settings);
    CHECK(result.width == 64 && result.height == 64);

    float maxLum = 0.0f;
    for (auto& p : result.pixels) maxLum = std::max(maxLum, (p.r + p.g + p.b) / 3.0f);
    std::printf("Lightmap bake max luminance: %.3f\n", maxLum);
    CHECK(maxLum > 0.01f); // should have picked up some direct light

    std::filesystem::create_directories("test_output");
    CHECK(Lightmapper::SaveAsPNG("test_output/test_lightmap.png", result));
}

int main() {
    TestECSAndTransforms();
    TestSceneSerialization();
    TestPhysicsFreeFall();
    TestScripting();
    TestBrushCSG();
    TestLightmapBake();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("\n%d TEST(S) FAILED\n", g_failures);
        return 1;
    }
}
