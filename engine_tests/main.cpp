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
#include "engine/render/SoftCanvas.h"
#include "engine/platform/Input.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
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
// Locks down the face-winding sign convention that BOTH rasterizers must obey.
//
// This is the regression test for the "all the shapes look inside-out" bug. The
// engine builds meshes counter-clockwise as seen from outside, and projects with
// a right-handed view matrix into [0,1] depth. Under that combination a triangle
// that FACES the camera lands on screen with a NEGATIVE signed area (screen Y
// points down, which mirrors the winding).
//
// Consequences, both of which are asserted here:
//   * SoftwareRenderer culls `area >= 0` - back faces, correct.
//   * D3D11 must be told FrontCounterClockwise = TRUE, because its default
//     (FALSE) means "front faces are clockwise / positive area" - the exact
//     inverse. Leaving it zero-initialised culled every outward face and drew
//     only the interior ones, which is what made the shapes look inverted.
static void TestFaceWindingSignConvention() {
    // A single triangle whose outward normal is +Z, wound CCW when viewed from
    // +Z (i.e. from where the camera sits).
    const vec3 tri[3] = { vec3(-1, -1, 0), vec3(1, -1, 0), vec3(0, 1, 0) };
    const vec3 outward(0, 0, 1);

    const vec3 geoNormal = glm::normalize(glm::cross(tri[1] - tri[0], tri[2] - tri[0]));
    CHECK(glm::dot(geoNormal, outward) > 0.9f);

    RenderCamera cam = MakeCamera(vec3(0, 0, 5), 0.0f, 0.0f, 60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    const mat4 viewProj = cam.ViewProj();

    // Project to the same screen space the rasterizer uses: NDC -> pixels with Y
    // flipped (top-left origin).
    const float w = 1280.0f, h = 720.0f;
    vec2 scr[3];
    for (int i = 0; i < 3; i++) {
        const vec4 clip = viewProj * vec4(tri[i], 1.0f);
        CHECK(clip.w > 0.0f);  // in front of the camera
        const vec3 ndc = vec3(clip) / clip.w;
        CHECK(ndc.z >= 0.0f && ndc.z <= 1.0f);  // [0,1] depth, not OpenGL's [-1,1]
        scr[i] = vec2((ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h);
    }

    const float area = (scr[1].x - scr[0].x) * (scr[2].y - scr[0].y) -
                       (scr[2].x - scr[0].x) * (scr[1].y - scr[0].y);
    std::printf("  camera-facing triangle signed screen area: %.1f\n", area);
    CHECK(area < 0.0f);  // <-- the whole convention in one assertion

    // And the mirror image: a triangle wound the other way (facing away) must
    // come out positive, so the two cases can never be confused.
    const vec2 flipped[3] = { scr[0], scr[2], scr[1] };
    const float backArea = (flipped[1].x - flipped[0].x) * (flipped[2].y - flipped[0].y) -
                           (flipped[2].x - flipped[0].x) * (flipped[1].y - flipped[0].y);
    CHECK(backArea > 0.0f);

    // Every builtin primitive must agree: sample each outward-facing triangle of
    // a cube from a camera placed along its normal and confirm the sign holds.
    MeshData cube;
    CHECK(MeshData::CreateBuiltin("builtin:cube", cube));
    CHECK(cube.indices.size() >= 36);
    int facing = 0;
    for (size_t i = 0; i + 2 < cube.indices.size(); i += 3) {
        const vec3 p0 = cube.vertices[cube.indices[i + 0]].position;
        const vec3 p1 = cube.vertices[cube.indices[i + 1]].position;
        const vec3 p2 = cube.vertices[cube.indices[i + 2]].position;
        const vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        const vec3 c = (p0 + p1 + p2) / 3.0f;

        // Look straight at this face from outside.
        RenderCamera fc;
        fc.position = c + n * 4.0f;
        fc.view = glm::lookAt(fc.position, c, std::fabs(n.y) > 0.9f ? vec3(0, 0, 1) : vec3(0, 1, 0));
        fc.proj = MakeProjectionMatrix(60.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        const mat4 vp = fc.ViewProj();

        vec2 s[3];
        bool ok = true;
        const vec3 src[3] = { p0, p1, p2 };
        for (int k = 0; k < 3; k++) {
            const vec4 clip = vp * vec4(src[k], 1.0f);
            if (clip.w <= 0.0f) { ok = false; break; }
            const vec3 ndc = vec3(clip) / clip.w;
            s[k] = vec2((ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h);
        }
        if (!ok) continue;
        const float a = (s[1].x - s[0].x) * (s[2].y - s[0].y) -
                        (s[2].x - s[0].x) * (s[1].y - s[0].y);
        CHECK(a < 0.0f);
        facing++;
    }
    std::printf("  cube: %d outward triangles, all negative-area when faced\n", facing);
    CHECK(facing == 12);

    // The D3D11 half of the convention lives in Windows-only code that this
    // headless suite cannot compile, let alone run. Guard it at the source
    // level instead: every rasterizer state that culls must opt in to
    // counter-clockwise front faces, because a zero-initialised
    // D3D11_RASTERIZER_DESC means FrontCounterClockwise = FALSE and would cull
    // exactly the faces we just proved are the visible ones.
    std::ifstream rf(Paths::Resolve("engine/src/render/Renderer.cpp"));
    const std::string rendererSrc((std::istreambuf_iterator<char>(rf)),
                                   std::istreambuf_iterator<char>());
    CHECK(!rendererSrc.empty());
    size_t declared = 0;
    for (size_t at = rendererSrc.find("FrontCounterClockwise"); at != std::string::npos;
         at = rendererSrc.find("FrontCounterClockwise", at + 1)) {
        const size_t eol = rendererSrc.find('\n', at);
        const std::string line = rendererSrc.substr(at, eol - at);
        if (line.find("TRUE") != std::string::npos || line.find("true") != std::string::npos)
            declared++;
    }
    std::printf("  Renderer.cpp sets FrontCounterClockwise = TRUE %zu time(s)\n", declared);
    CHECK(declared >= 2);  // the default/cull-back state and the shadow state
}

// MakeCamera takes a yaw/pitch pair, and the inverse of that mapping is
// yaw = atan2(-dx, -dz). Getting the sign backwards aims the camera 180 degrees
// away from where you meant - which is exactly how fwshot's "side" preset ended
// up rendering zero triangles and "persp" only three. Pin the round-trip down.
// Click-to-select in the viewport. The editor could previously only pick
// brushes, so clicking the cube/sphere/ramp selected nothing and the Hierarchy
// panel was the only way to choose an object.
//
// EditorApp itself is Windows-only, so this exercises the two pieces of maths
// its picking is built from - the screen-pixel-to-world-ray unprojection and
// ray/triangle intersection - against the real starter scene. If a click at the
// centre of the screen doesn't hit the object the camera is pointed at, picking
// is broken regardless of the UI wiring.
static void TestViewportPickingMath() {
    Scene scene;
    DefaultScene::Build(scene, nullptr);
    scene.UpdateTransforms();

    const float w = 1280.0f, h = 720.0f;
    // Unprojects a pixel to a world ray, mirroring EditorApp::ScreenPointToRay.
    auto rayThrough = [&](const RenderCamera& cam, float px, float py) {
        const float ndcX = (px / w) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (py / h) * 2.0f;
        const mat4 inv = glm::inverse(cam.ViewProj());
        vec4 n = inv * vec4(ndcX, ndcY, 0.0f, 1.0f);
        vec4 f = inv * vec4(ndcX, ndcY, 1.0f, 1.0f);
        n /= n.w; f /= f.w;
        Ray r;
        r.origin = vec3(n);
        r.direction = glm::normalize(vec3(f) - vec3(n));
        return r;
    };

    // Closest mesh entity along the ray, exact triangles in local space.
    auto pick = [&](const Ray& ray, float& outT) {
        entt::entity best = entt::null;
        outT = std::numeric_limits<float>::max();
        scene.Each<TransformComponent, MeshRendererComponent>(
            [&](Entity e, TransformComponent& tc, MeshRendererComponent& mr) {
                MeshData mesh;
                if (!MeshData::LoadAny(mr.meshAsset, mesh)) return;
                const mat4 world = tc.worldMatrix;
                const mat4 invWorld = glm::inverse(world);
                Ray lr;
                lr.origin = vec3(invWorld * vec4(ray.origin, 1.0f));
                lr.direction = glm::normalize(vec3(invWorld * vec4(ray.direction, 0.0f)));
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const vec3& p0 = mesh.vertices[mesh.indices[i + 0]].position;
                    const vec3& p1 = mesh.vertices[mesh.indices[i + 1]].position;
                    const vec3& p2 = mesh.vertices[mesh.indices[i + 2]].position;
                    float t;
                    if (!RayTriangleIntersect(lr, p0, p1, p2, t)) continue;
                    const vec3 hitW = vec3(world * vec4(lr.At(t), 1.0f));
                    const float tw = glm::dot(hitW - ray.origin, ray.direction);
                    if (tw >= 0.0f && tw < outT) { outT = tw; best = e.Handle(); }
                }
            });
        return best;
    };

    // Aim a camera straight at each object and click the centre pixel: the
    // object under the crosshair must be the one that gets picked.
    struct Case { const char* name; };
    const Case cases[] = { {"Box"}, {"Sphere"}, {"Cylinder"}, {"Ramp"} };
    int verified = 0;
    for (const Case& c : cases) {
        entt::entity target = entt::null;
        vec3 targetPos(0.0f);
        scene.Each<TransformComponent, MeshRendererComponent>(
            [&](Entity e, TransformComponent& tc, MeshRendererComponent&) {
                if (e.Name().rfind(c.name, 0) == 0 && target == entt::null) {
                    target = e.Handle();
                    targetPos = vec3(tc.worldMatrix[3]);
                }
            });
        if (target == entt::null) continue;

        // Look at the object from directly outside it, along the axis pointing
        // away from the scene centre. Approaching from a fixed direction would
        // put other props in the way (the Sphere sits between the origin and
        // the Cylinder), and picking the nearer object would then be correct
        // behaviour rather than a bug.
        vec3 away = targetPos - vec3(0.0f, targetPos.y, 0.0f);
        if (glm::length2(away) < 1e-4f) away = vec3(0.0f, 0.0f, 1.0f);  // object on the axis
        const vec3 eye = targetPos + glm::normalize(away) * 4.0f + vec3(0.0f, 1.2f, 0.0f);
        RenderCamera cam;
        cam.position = eye;
        cam.view = glm::lookAt(eye, targetPos, vec3(0, 1, 0));
        cam.proj = MakeProjectionMatrix(60.0f, w / h, 0.05f, 2000.0f);

        float t = 0.0f;
        const entt::entity hit = pick(rayThrough(cam, w * 0.5f, h * 0.5f), t);
        CHECK(hit == target);
        CHECK(t > 0.0f);
        verified++;
    }
    std::printf("  centre-screen click selects the aimed-at entity (%d objects)\n", verified);
    CHECK(verified == 4);

    // A ray pointed at empty sky must select nothing, so clicking the
    // background deselects instead of grabbing whatever happens to be closest.
    RenderCamera sky;
    sky.position = vec3(0.0f, 3.0f, 12.0f);
    sky.view = glm::lookAt(sky.position, sky.position + vec3(0, 1, 0), vec3(0, 0, -1));
    sky.proj = MakeProjectionMatrix(60.0f, w / h, 0.05f, 2000.0f);
    float tSky = 0.0f;
    CHECK(pick(rayThrough(sky, w * 0.5f, h * 0.5f), tSky) == entt::null);

    // Off-centre pixels must map to different rays, otherwise every click on
    // the viewport would resolve to the same point.
    RenderCamera cam;
    cam.position = vec3(0.0f, 3.0f, 12.0f);
    cam.view = glm::lookAt(cam.position, vec3(0.0f), vec3(0, 1, 0));
    cam.proj = MakeProjectionMatrix(60.0f, w / h, 0.05f, 2000.0f);
    const Ray left = rayThrough(cam, w * 0.25f, h * 0.5f);
    const Ray right = rayThrough(cam, w * 0.75f, h * 0.5f);
    CHECK(glm::dot(left.direction, right.direction) < 0.999f);
    // The left pixel must point to the left of the centre ray.
    const Ray mid = rayThrough(cam, w * 0.5f, h * 0.5f);
    CHECK(glm::dot(glm::cross(left.direction, mid.direction), vec3(0, 1, 0)) < 0.0f);
    std::printf("  screen-to-ray unprojection is directional and empty space picks nothing\n");
}

// The grid hotkeys step in powers of two and clamp, matching Hammer.
static void TestGridStepping() {
    const float kMin = 0.03125f, kMax = 128.0f;
    auto step = [&](float g, int dir) {
        if (dir < 0) return std::max(g * 0.5f, kMin);
        if (dir > 0) return std::min(g * 2.0f, kMax);
        return g;
    };

    float g = 1.0f;
    g = step(g, +1); CHECK(g == 2.0f);
    g = step(g, +1); CHECK(g == 4.0f);
    g = step(g, -1); CHECK(g == 2.0f);
    g = step(g, -1); CHECK(g == 1.0f);

    // Clamps rather than running away in either direction.
    for (int i = 0; i < 40; i++) g = step(g, -1);
    CHECK(g == kMin);
    for (int i = 0; i < 80; i++) g = step(g, +1);
    CHECK(g == kMax);

    // Every reachable size is a clean power of two, so the grid always lines up
    // with itself when you zoom between steps.
    g = kMin;
    while (g < kMax) {
        const float l = std::log2(g);
        CHECK(std::fabs(l - std::round(l)) < 1e-5f);
        g = step(g, +1);
    }
    std::printf("  grid stepping: powers of two from %g to %g, clamped\n", kMin, kMax);
}

static void TestCameraYawSign() {
    struct Case { const char* name; vec3 pos; };
    const Case cases[] = {
        { "front", vec3(0.0f, 3.0f, 16.0f) },
        { "side",  vec3(16.0f, 3.0f, 0.0f) },
        { "persp", vec3(10.0f, 7.0f, 13.0f) },
        { "behind", vec3(-8.0f, 4.0f, -9.0f) },
    };
    const vec3 target(0.0f, 0.5f, 0.0f);

    for (const Case& c : cases) {
        const vec3 d = glm::normalize(target - c.pos);
        const float yaw = Degrees(std::atan2(-d.x, -d.z));
        const float pitch = Degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));

        RenderCamera cam = MakeCamera(c.pos, yaw, pitch, 60.0f, 16.0f / 9.0f, 0.05f, 2000.0f);
        // The camera derived from those angles must actually look at the target.
        const float alignment = glm::dot(cam.Forward(), d);
        std::printf("  %-7s yaw %+7.1f pitch %+6.1f -> forward alignment %.4f\n",
                    c.name, yaw, pitch, alignment);
        CHECK(alignment > 0.999f);

        // And the target must land inside the frustum, in front of the camera.
        const vec4 clip = cam.ViewProj() * vec4(target, 1.0f);
        CHECK(clip.w > 0.0f);
        const vec3 ndc = vec3(clip) / clip.w;
        CHECK(std::fabs(ndc.x) <= 1.0f);
        CHECK(std::fabs(ndc.y) <= 1.0f);
    }
}

// Input::NewFrame() must run at the TOP of the frame, before the platform
// message pump, and OnUpdate() must then see everything that arrived during
// that pump. The loop originally pumped messages first and called NewFrame()
// afterwards, which wiped the pressed/released sets and the wheel delta before
// any consumer could read them: WasKeyPressed() and WheelDelta() were dead, so
// the editor's "F to frame" and wheel speed control silently did nothing.
//
// This simulates one full frame in the documented order and asserts the
// edge-triggered state survives to where OnUpdate() reads it.
static void TestInputFrameOrdering() {
    Input& in = Input::Get();
    in.ResetState();

    // ---- frame 1: press W, move the mouse, spin the wheel ------------------
    in.NewFrame();          // top of frame, before the pump
    in.OnKeyDown('W');      // <- messages pumped here
    in.OnMouseMove(100, 50);
    in.OnMouseMove(110, 60);
    in.OnMouseWheel(2.0f);

    // OnUpdate() reads here.
    CHECK(in.IsKeyDown('W'));
    CHECK(in.WasKeyPressed('W'));        // the edge must still be visible
    CHECK(in.WheelDelta() == 2.0f);      // and so must the accumulated wheel
    CHECK(in.MouseDelta().x == 110.0f);  // delta measured from frame start (0,0)
    CHECK(in.MouseDelta().y == 60.0f);

    // ---- frame 2: nothing happens ------------------------------------------
    in.NewFrame();
    CHECK(in.IsKeyDown('W'));            // still held: level-triggered
    CHECK(!in.WasKeyPressed('W'));       // but no longer a fresh press
    CHECK(in.WheelDelta() == 0.0f);      // wheel is per-frame
    CHECK(in.MouseDelta().x == 0.0f);    // no motion this frame
    CHECK(in.MouseDelta().y == 0.0f);

    // ---- frame 3: release W, drag the mouse further ------------------------
    in.NewFrame();
    in.OnKeyUp('W');
    in.OnMouseMove(130, 60);
    CHECK(!in.IsKeyDown('W'));
    CHECK(in.WasKeyReleased('W'));
    CHECK(in.MouseDelta().x == 20.0f);   // 130 - 110, relative to frame start

    // ---- mouse buttons drive the camera capture latch ----------------------
    in.NewFrame();
    in.OnMouseButton(1, true);
    CHECK(in.IsMouseButtonDown(1));
    in.NewFrame();
    CHECK(in.IsMouseButtonDown(1));      // held across frames until released
    in.OnMouseButton(1, false);
    CHECK(!in.IsMouseButtonDown(1));

    // ---- focus loss must not leave keys stuck down -------------------------
    in.NewFrame();
    in.OnKeyDown('A');
    CHECK(in.IsKeyDown('A'));
    in.ResetState();                     // WM_KILLFOCUS
    CHECK(!in.IsKeyDown('A'));
    CHECK(in.MouseDelta().x == 0.0f);    // and no phantom jump on refocus

    // ---- UI capture flags round-trip ---------------------------------------
    in.SetUICapture(true, false);
    CHECK(in.UIWantsKeyboard());
    CHECK(!in.UIWantsMouse());
    in.SetUICapture(false, true);
    CHECK(!in.UIWantsKeyboard());
    CHECK(in.UIWantsMouse());
    in.ResetState();
    in.SetUICapture(false, false);
    std::printf("  input frame ordering, edge state, capture flags: ok\n");
}

// The main loop's ordering is the other half of the contract above, and it
// lives in Windows-only code this suite cannot execute. Assert it at the
// source level so the two calls can't be swapped back.
static void TestMainLoopPumpsAfterNewFrame() {
    std::ifstream f(Paths::Resolve("engine/src/core/Application.cpp"));
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!src.empty());

    const size_t loop = src.find("void Application::MainLoop()");
    CHECK(loop != std::string::npos);
    const size_t newFrame = src.find("Input::Get().NewFrame()", loop);
    const size_t pump = src.find("m_Window.PumpMessages()", loop);
    const size_t onUpdate = src.find("OnUpdate(dt)", loop);
    CHECK(newFrame != std::string::npos);
    CHECK(pump != std::string::npos);
    CHECK(onUpdate != std::string::npos);

    // NewFrame -> PumpMessages -> OnUpdate, in that order.
    CHECK(newFrame < pump);
    CHECK(pump < onUpdate);
    std::printf("  MainLoop order: NewFrame -> PumpMessages -> OnUpdate\n");
}

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

// The editor must never be able to show a blank window. The software
// presentation path (SoftCanvas + the CPU viewport renderer) is what guarantees
// that, and it is fully testable on any host: build an ImGui frame, rasterize it
// and check that real pixels came out.
static void TestSoftwareCanvasIsNotBlank() {
    SoftCanvas canvas;
    canvas.Resize(640, 360);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(640.0f, 360.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // SoftCanvas creates the font atlas
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    for (int frame = 0; frame < 3; frame++) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(600.0f, 320.0f));
        ImGui::Begin("Forgeworks");
        ImGui::Text("The editor must draw something here.");
        ImGui::Button("Play");
        ImGui::End();
        ImGui::Render();
        canvas.Clear(IM_COL32(13, 13, 16, 255));
        canvas.RenderImGuiFrame();
    }

    const ImDrawData* dd = ImGui::GetDrawData();
    CHECK(dd != nullptr);
    if (dd) {
        std::printf("  UI draw data: %d list(s), %d verts, %d indices\n",
                    dd->CmdListsCount, dd->TotalVtxCount, dd->TotalIdxCount);
        CHECK(dd->CmdListsCount > 0);
        CHECK(dd->TotalVtxCount > 0);
    }

    int changed = 0;
    int distinct[1 << 15] = {0};  // 5 bits per channel; keep in step with the shifts below
    int distinctCount = 0;
    const u8* pixels = canvas.Pixels();
    for (int i = 0; i < canvas.Width() * canvas.Height(); i++) {
        const u8* px = pixels + (size_t)i * 4;
        if (px[0] != 13 || px[1] != 13 || px[2] != 16) changed++;
        const int key = ((px[0] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[2] >> 3);
        if (!distinct[key]) { distinct[key] = 1; distinctCount++; }
    }
    const float coverage = (float)changed / (float)(canvas.Width() * canvas.Height());
    std::printf("  UI coverage: %.1f%%, distinct colours: %d\n", coverage * 100.0f, distinctCount);
    CHECK(coverage > 0.05f);
    CHECK(distinctCount > 8);

    // The font atlas must have arrived through the texture-request contract.
    CHECK(canvas.HasTexture(1));

    ImGui::DestroyContext();
}

// ---------------------------------------------------------------------------
// Shader source lint (no HLSL compiler required)
// ---------------------------------------------------------------------------
// A shader that fails to compile can only be caught by D3DCompile, which is not
// available in every build environment - and shipping one is exactly how the
// editor ended up with a broken grid shader: `float line = ...`, where `line` is
// a reserved HLSL keyword (geometry-shader primitive), rejected with
// "error X3000: syntax error: unexpected token 'line'".
//
// This is not a compiler, but it catches that class of mistake before a build
// reaches a Windows box:
//   * geometry-shader primitive keywords (line, point, triangle, ...) used as
//     identifiers anywhere - these have no legitimate use in the engine's
//     vertex/pixel shaders
//   * any reserved keyword used as a declared name (`float matrix = ...`)
//   * a missing VSMain/PSMain entry point
//   * unbalanced braces
//   * non-ASCII bytes (D3DCompileFromFile refuses the source)
static void TestShaderSourcesAreLintable() {
    // Keywords that are *always* wrong in a vertex/pixel shader.
    const std::vector<std::string> alwaysWrong = {
        "line", "point", "lineadj", "triangle", "triangleadj",
        "pixelshader", "vertexshader", "geometryshader", "hullshader",
        "domainshader", "computeshader", "technique10", "technique11",
    };
    // Keywords that are fine as language constructs ("struct Foo {", "discard;")
    // but wrong as a name - only flagged when used as a declaration name.
    const std::vector<std::string> wrongAsName = {
        "line", "point", "triangle", "matrix", "vector", "texture", "sampler",
        "cbuffer", "tbuffer", "struct", "string", "discard", "pass", "technique",
    };
    const std::vector<std::string> typeKeywords = {
        "float", "float2", "float3", "float4", "float2x2", "float3x3", "float4x4",
        "int", "int2", "int3", "int4", "uint", "uint2", "uint3", "uint4",
        "bool", "half", "double", "static", "const",
    };

    auto isIdentChar = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    auto tokens = [&](const std::string& code) {
        std::vector<std::string> out;
        std::string current;
        for (char c : code) {
            if (isIdentChar(c)) {
                current.push_back(c);
            } else {
                if (!current.empty()) out.push_back(current);
                current.clear();
                out.push_back(std::string(1, c));
            }
        }
        if (!current.empty()) out.push_back(current);
        return out;
    };
    auto isType = [&](const std::string& t) {
        return std::find(typeKeywords.begin(), typeKeywords.end(), t) != typeKeywords.end();
    };

    const std::filesystem::path shaderDir = "assets/shaders";
    CHECK(std::filesystem::exists(shaderDir));

    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(shaderDir)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".hlsl" && ext != ".hlsli") continue;
        files++;

        std::ifstream file(entry.path());
        CHECK(file.good());
        if (!file.good()) continue;

        std::string line, source;
        int lineNumber = 0;
        while (std::getline(file, line)) {
            lineNumber++;
            source += line + "\n";

            const size_t comment = line.find("//");
            const std::string code = comment == std::string::npos ? line : line.substr(0, comment);
            for (unsigned char c : code) {
                if (c > 127) {
                    std::printf("FAIL: %s:%d: non-ASCII byte 0x%02X in shader source\n",
                                entry.path().string().c_str(), lineNumber, (unsigned)c);
                    g_failures++;
                    break;
                }
            }

            const std::vector<std::string> tk = tokens(code);
            for (size_t i = 0; i < tk.size(); i++) {
                const std::string& t = tk[i];
                if (t.empty() || isIdentChar(t[0]) == false) continue;

                const bool reserved = std::find(alwaysWrong.begin(), alwaysWrong.end(), t) != alwaysWrong.end();
                bool flagged = reserved;

                // `float line = ...` / `float line,` / `float line)` - a declaration.
                if (!flagged && std::find(wrongAsName.begin(), wrongAsName.end(), t) != wrongAsName.end()) {
                    if (i >= 2 && isType(tk[i - 1])) {
                        const std::string& next = i + 1 < tk.size() ? tk[i + 1] : std::string();
                        if (next == "=" || next == ";" || next == "," || next == ")" || next == "[") flagged = true;
                    }
                }
                if (flagged) {
                    std::printf("FAIL: %s:%d: '%s' is a reserved HLSL keyword and cannot be used "
                                "as an identifier\n",
                                entry.path().string().c_str(), lineNumber, t.c_str());
                    g_failures++;
                }
            }
        }

        const int braces = (int)std::count(source.begin(), source.end(), '{') -
                           (int)std::count(source.begin(), source.end(), '}');
        if (braces != 0) {
            std::printf("FAIL: %s: unbalanced braces (%d)\n", entry.path().string().c_str(), braces);
            g_failures++;
        }

        if (ext == ".hlsl") {
            const bool isDepthOnly = entry.path().filename().string().find("ShadowDepth") != std::string::npos;
            if (source.find("VSMain") == std::string::npos) {
                std::printf("FAIL: %s: no VSMain entry point - the engine compiles it with VSMain\n",
                            entry.path().string().c_str());
                g_failures++;
            }
            if (!isDepthOnly && source.find("PSMain") == std::string::npos) {
                std::printf("FAIL: %s: no PSMain entry point\n", entry.path().string().c_str());
                g_failures++;
            }
        }
    }

    std::printf("  shader lint: %d file(s) checked in %s\n", files, shaderDir.string().c_str());
    CHECK(files >= 4);  // Mesh, ShadowDepth, Skybox, Grid (+ Common.hlsli)
}

int main() {
    std::printf("== paths + starter content ==\n");     TestPathsAndStarterContent();
    std::printf("== hammer brush compile ==\n");        TestBrushCompilePipeline();
    std::printf("== ecs + transforms ==\n");            TestECSAndTransforms();
    std::printf("== scene serialization ==\n");         TestSceneSerialization();
    std::printf("== camera / projection math ==\n");    TestCameraMatrices();
    std::printf("== primitive winding ==\n");           TestPrimitiveWinding();
    std::printf("== face winding sign convention ==\n"); TestFaceWindingSignConvention();
    std::printf("== camera yaw sign ==\n");              TestCameraYawSign();
    std::printf("== viewport picking ==\n");            TestViewportPickingMath();
    std::printf("== grid stepping ==\n");               TestGridStepping();
    std::printf("== input frame ordering ==\n");        TestInputFrameOrdering();
    std::printf("== main loop ordering ==\n");          TestMainLoopPumpsAfterNewFrame();
    std::printf("== viewport renders something ==\n");  TestViewportIsNotBlank();
    std::printf("== physics ==\n");                     TestPhysicsFreeFall();
    std::printf("== scripting ==\n");                   TestScripting();
    std::printf("== brush csg ==\n");                   TestBrushCSG();
    std::printf("== lightmap bake ==\n");               TestLightmapBake();
    std::printf("== software canvas draws the UI ==\n"); TestSoftwareCanvasIsNotBlank();
    std::printf("== shader sources ==\n");            TestShaderSourcesAreLintable();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::printf("\n%d TEST(S) FAILED\n", g_failures);
        return 1;
    }
}
