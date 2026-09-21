// Minimal smoke tests for the platform-independent engine core. This is not
// a full test suite (see docs/ for how these validate logic without a
// Windows/D3D11 host) - it exercises ECS, scene serialization, physics,
// scripting and brush/CSG so regressions in the core simulation are caught
// on any platform.
#include "engine/core/Engine.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/DefaultScene.h"
#include "engine/brush/Brush.h"
#include "engine/brush/BrushMap.h"
#include "engine/lightmap/Lightmapper.h"
#include "engine/render/RenderTypes.h"
#include "engine/render/SoftwareRenderer.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

// The viewport screenshot test writes a real PNG so the rendered frame can be
// inspected by a human (this is how "the editor shows nothing" was diagnosed).
// STB_IMAGE_WRITE_IMPLEMENTATION lives in engine/src/lightmap/Lightmapper.cpp,
// so the symbols come from fw_engine - only the declarations are needed here.
#include <stb_image_write.h>

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


// ---------------------------------------------------------------------------
// Camera / projection convention.
//
// Regression test for the bug that made the editor's viewport completely
// blank: view space is right handed (looking down -Z), so the projection must
// use the Direct3D-style [0,1] depth range, and a point in front of the camera
// must land inside the clip volume. If this test fails, nothing renders.
// ---------------------------------------------------------------------------
static void TestCameraMatrices() {
    const float aspect = 16.0f / 9.0f;
    const mat4 proj = MakeProjectionMatrix(60.0f, aspect, 0.05f, 2000.0f);
    const vec3 eye(0, 0, 0);
    const mat4 view = MakeViewMatrix(eye, 0.0f, 0.0f); // yaw 0 => looking down -Z
    const mat4 viewProj = proj * view;

    // A point 10 units straight ahead maps to the centre of the screen with a
    // depth inside [0,1].
    vec4 clip = viewProj * vec4(0, 0, -10, 1);
    CHECK(clip.w > 0.0f);                       // in front of the camera
    vec3 ndc = vec3(clip) / clip.w;
    CHECK(std::abs(ndc.x) < 1e-3f);
    CHECK(std::abs(ndc.y) < 1e-3f);
    CHECK(ndc.z > 0.0f && ndc.z < 1.0f);        // D3D-style [0,1] depth range

    // Near/far planes map to exactly 0 and 1.
    vec4 nearClip = viewProj * vec4(0, 0, -0.05f, 1);
    vec4 farClip = viewProj * vec4(0, 0, -2000.0f, 1);
    CHECK(std::abs(nearClip.z / nearClip.w) < 1e-3f);
    CHECK(std::abs(farClip.z / farClip.w - 1.0f) < 1e-3f);

    // A point behind the camera is behind the near plane (z < 0), which is what
    // the near-plane clip in the rasterizers relies on.
    vec4 behind = viewProj * vec4(0, 0, 10, 1);
    CHECK(behind.w < 0.0f || behind.z < 0.0f);

    // +X must appear on the right of the screen, +Y at the top (D3D-style
    // viewport where NDC y grows upwards before the y-flip).
    vec4 right = viewProj * vec4(2, 0, -10, 1);
    CHECK(right.x / right.w > 0.0f);
    vec4 up = viewProj * vec4(0, 2, -10, 1);
    CHECK(up.y / up.w > 0.0f);

    // The yaw/pitch helper must agree with the view matrix it builds...
    const vec3 fwd = YawPitchForward(35.0f, -20.0f);
    const RenderCamera probe = MakeCamera(vec3(1, 2, 3), 35.0f, -20.0f, 60.0f, 1.0f, 0.05f, 100.0f);
    CHECK(glm::distance(fwd, probe.Forward()) < 1e-4f);
    CHECK(fwd.z < 0.0f); // still looking "forward" (-Z) for yaw in (-90, 90)

    // ...and with the euler convention used by entity transforms, so a
    // CameraComponent authored in the editor looks where the editor camera did.
    for (const float yaw : { -70.0f, -20.0f, 0.0f, 15.0f, 55.0f }) {
        for (const float pitchDeg : { -60.0f, -15.0f, 0.0f, 30.0f }) {
            Transform t;
            t.SetEulerDegrees(vec3(pitchDeg, yaw, 0.0f));
            CHECK(glm::distance(YawPitchForward(yaw, pitchDeg), t.Forward()) < 1e-4f);
        }
    }
}

// ---------------------------------------------------------------------------
// Primitive winding: every triangle of every built-in primitive must face
// outwards, otherwise backface culling makes meshes invisible (this is exactly
// what happened to builtin:sphere / the cylinder caps before).
// ---------------------------------------------------------------------------
static void TestPrimitiveWinding() {
    for (const char* const* name = MeshData::BuiltinPrimitiveNames(); *name; ++name) {
        MeshData mesh;
        CHECK(MeshData::CreateBuiltin(*name, mesh));
        if (mesh.indices.size() < 3) { CHECK(false); continue; }

        vec3 centroid(0.0f);
        for (auto& v : mesh.vertices) centroid += v.position;
        centroid /= (float)mesh.vertices.size();

        // For flat primitives (the plane) "outward from the centroid" is
        // degenerate, so require an up-facing normal instead.
        const bool flat = std::string(*name) == "builtin:plane";
        int checked = 0;
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const vec3 p0 = mesh.vertices[mesh.indices[i + 0]].position;
            const vec3 p1 = mesh.vertices[mesh.indices[i + 1]].position;
            const vec3 p2 = mesh.vertices[mesh.indices[i + 2]].position;
            const vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::length2(n) < 1e-12f) continue; // degenerate triangle
            const vec3 nn = glm::normalize(n);
            if (flat) {
                CHECK(nn.y > 0.9f);
            } else {
                const vec3 triCentroid = (p0 + p1 + p2) / 3.0f;
                const vec3 outward = glm::normalize(triCentroid - centroid);
                CHECK(glm::dot(nn, outward) > 0.0f);
            }
            checked++;
        }
        std::printf("  %-18s %zu verts, %zu tris (%d checked)\n", *name, mesh.vertices.size(),
                    mesh.indices.size() / 3, checked);
        CHECK(checked > 0);

        // Stored vertex normals should agree with the winding too.
        for (auto& v : mesh.vertices) {
            const vec3 outward = v.position - centroid;
            if (glm::length2(outward) < 1e-8f) continue;
            CHECK(glm::dot(v.normal, glm::normalize(outward)) > -0.35f);
        }
    }
}

// ---------------------------------------------------------------------------
// "The viewport must not be blank": render the starter scene with the CPU
// reference renderer and require real geometry to be visible, plus a PNG on
// disk to look at.
// ---------------------------------------------------------------------------
static void TestViewportIsNotBlank() {
    Scene scene("ViewportTest");
    DefaultScene::Build(scene, nullptr);

    Entity camera = scene.PrimaryCamera();
    CHECK((bool)camera);
    if (!camera) return;

    auto& tc = camera.Get<TransformComponent>();
    auto& cc = camera.Get<CameraComponent>();

    RenderCamera rc;
    rc.position = vec3(tc.worldMatrix[3]);
    rc.view = glm::inverse(tc.worldMatrix);
    rc.proj = MakeProjectionMatrix(cc.fovDeg, 16.0f / 9.0f, cc.nearClip, cc.farClip);

    RenderSettings settings;
    settings.drawGrid = true;
    auto skyView = scene.Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = scene.Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }

    SoftwareImage image;
    image.Resize(640, 360);
    SoftwareRenderStats stats;
    SoftwareRenderer::RenderScene(scene, rc, settings, image, &stats);

    std::printf("  viewport: %d mesh entities, %d tris drawn, %d culled, geometry coverage %.1f%%, %d lights\n",
                stats.meshEntities, stats.drawnTriangles, stats.culledTriangles,
                stats.GeometryCoverage() * 100.0f, stats.lightCount);

    CHECK(stats.meshEntities >= 5);                  // the starter scene has geometry
    CHECK(stats.drawnTriangles > 0);                 // and it is actually rasterised
    CHECK(stats.GeometryCoverage() > 0.05f);         // covering a real part of the image
    CHECK(stats.lightCount >= 2);                    // sun + point light

    // Not a flat image either: count distinct colours.
    std::set<u32> colors;
    for (int i = 0; i < image.width * image.height; i++) {
        const u8* p = image.Data() + i * 4;
        colors.insert((u32)p[0] << 16 | (u32)p[1] << 8 | p[2]);
        if (colors.size() > 64) break;
    }
    CHECK(colors.size() > 8);

    // Same picture, but from the top of the scene looking down, to be sure the
    // result isn't a coincidence of this particular camera angle.
    RenderCamera top = MakeCamera(vec3(0.0f, 18.0f, 0.01f), 0.0f, -89.0f, 60.0f, 16.0f / 9.0f, 0.05f, 2000.0f);
    SoftwareImage topImage;
    topImage.Resize(320, 180);
    SoftwareRenderStats topStats;
    SoftwareRenderer::RenderScene(scene, top, settings, topImage, &topStats);
    std::printf("  top-down: %d tris, geometry coverage %.1f%%\n",
                topStats.drawnTriangles, topStats.GeometryCoverage() * 100.0f);
    CHECK(topStats.GeometryCoverage() > 0.02f);

    // Write a PNG so the rendered frame can be inspected visually.
    std::filesystem::create_directories("test_output");
    const int written = stbi_write_png("test_output/viewport.png", image.width, image.height, 4,
                                       image.Data(), image.width * 4);
    CHECK(written != 0);
    std::printf("  wrote test_output/viewport.png (%dx%d)\n", image.width, image.height);
}

// ---------------------------------------------------------------------------
// Path resolution + starter content on disk.
// ---------------------------------------------------------------------------
static void TestPathsAndStarterContent() {
    Paths::Initialize();
    std::printf("  project root: %s (from %s)\n", Paths::ProjectRoot().c_str(), Paths::RootSource().c_str());
    CHECK(Paths::FoundProjectRoot());
    CHECK(std::filesystem::exists(Paths::Resolve("assets/shaders/Mesh.hlsl")));
    CHECK(std::filesystem::exists(Paths::Resolve("assets/shaders/Grid.hlsl")));
    CHECK(Paths::Resolve("assets/shaders/Mesh.hlsl") != "assets/shaders/Mesh.hlsl" ||
          std::filesystem::exists("assets/shaders/Mesh.hlsl"));

    // The starter scene must be creatable, loadable, and non-empty.
    std::filesystem::create_directories("test_output");
    const std::string scenePath = "test_output/starter.fwscene";
    std::filesystem::remove(scenePath);
    std::filesystem::remove(scenePath + ".brushes.json");
    CHECK(DefaultScene::EnsureStarterContent(scenePath));
    CHECK(std::filesystem::exists(scenePath));

    Scene loaded;
    BrushMap brushes;
    CHECK(SceneSerializer::Load(loaded, scenePath, &brushes));
    const int entities = (int)loaded.Registry().view<IDComponent>().size();
    std::printf("  starter scene: %d entities, %zu brushes\n", entities, brushes.Brushes().size());
    CHECK(entities >= 8);
    CHECK((bool)loaded.PrimaryCamera());
    CHECK(!brushes.Brushes().empty());
    CHECK(loaded.FindByName("Sun (Directional Light)").IsValid());
}

// ---------------------------------------------------------------------------
// Hammer mode: brushes must compile into renderable entities with real
// material slots (a brush face names a *texture*; the renderer needs a .fwmat).
// Before this was bridged, brush geometry silently rendered with the default
// material, and the compiled mesh file was written to a working-directory
// relative path.
// ---------------------------------------------------------------------------
static void TestBrushCompilePipeline() {
    Scene scene("HammerTest");
    BrushMap brushes;

    Brush block = Brush::CreateBox(vec3(-1, -1, -1), vec3(1, 1, 1));
    block.Rebuild();
    Brush& added = brushes.AddBrush(block);
    const UUID blockId = added.id;
    CHECK(brushes.Brushes().size() == 1);

    std::filesystem::create_directories("test_output");
    brushes.CompileToScene(scene, "test_output/generated");

    Entity brushEntity = scene.FindByUUID(blockId);
    CHECK((bool)brushEntity);
    if (brushEntity) {
        CHECK(brushEntity.Has<MeshRendererComponent>());
        CHECK(brushEntity.Has<ColliderComponent>());
        CHECK(brushEntity.Has<BrushComponent>());
        auto& mr = brushEntity.Get<MeshRendererComponent>();
        std::printf("  brush mesh: %s\n", mr.meshAsset.c_str());
        CHECK(!mr.meshAsset.empty());
        CHECK(std::filesystem::exists(mr.meshAsset));
        CHECK(!mr.materialSlots.empty());
        if (!mr.materialSlots.empty()) {
            std::printf("  brush material slot: %s\n", mr.materialSlots[0].c_str());
            CHECK(mr.materialSlots[0].size() > 6);
            CHECK(mr.materialSlots[0].compare(mr.materialSlots[0].size() - 6, 6, ".fwmat") == 0);
            CHECK(std::filesystem::exists(mr.materialSlots[0]));
        }
        // The compiled brush must be renderable by the CPU renderer too.
        scene.UpdateTransforms();
        RenderCamera cam = MakeCamera(vec3(0, 0, 5), 0.0f, 0.0f, 60.0f, 1.0f, 0.05f, 100.0f);
        RenderSettings settings;
        settings.drawGrid = false;
        SoftwareImage image;
        image.Resize(160, 160);
        SoftwareRenderStats stats;
        SoftwareRenderer::RenderScene(scene, cam, settings, image, &stats);
        std::printf("  brush renders %d triangles, %.1f%% coverage\n",
                    stats.drawnTriangles, stats.GeometryCoverage() * 100.0f);
        CHECK(stats.drawnTriangles > 0);
        CHECK(stats.GeometryCoverage() > 0.05f);
    }
}

int main() {
    std::printf("== paths + starter content ==\n");     TestPathsAndStarterContent();
    std::printf("== hammer brush compile ==\n");        TestBrushCompilePipeline();
    std::printf("== ecs + transforms ==\n");            TestECSAndTransforms();
    std::printf("== scene serialization ==\n");         TestSceneSerialization();
    std::printf("== camera / projection math ==\n");    TestCameraMatrices();
    std::printf("== primitive winding ==\n");           TestPrimitiveWinding();
    std::printf("== viewport renders something ==\n");  TestViewportIsNotBlank();
    std::printf("== physics ==\n");                     TestPhysicsFreeFall();
    std::printf("== scripting ==\n");                   TestScripting();
    std::printf("== brush csg ==\n");                   TestBrushCSG();
    std::printf("== lightmap bake ==\n");               TestLightmapBake();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("\n%d TEST(S) FAILED\n", g_failures);
        return 1;
    }
}
