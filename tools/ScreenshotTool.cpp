// fwshot: renders a .fwscene to a PNG from the command line.
//
//   fwshot <scene.fwscene> <out.png> [options]
//     --width  <px>        image width  (default 1280)
//     --height <px>        image height (default 720)
//     --camera <mode>      scene | top | front | side | persp   (default: scene)
//     --pos x,y,z          explicit camera position
//     --look x,y,z         explicit look-at target
//     --yaw <deg>          explicit yaw (overrides --look)
//     --pitch <deg>        explicit pitch (overrides --look)
//     --fov <deg>          field of view (default 60)
//     --wireframe          wireframe shading
//     --no-grid            hide the ground grid
//
// It uses the engine's CPU reference renderer (engine/render/SoftwareRenderer.h)
// which reproduces the D3D11 renderer's camera math and passes on the CPU, so it
// works on any platform, in CI, and without a GPU. This is the tool used to
// produce the screenshots in docs/screenshots/.

#include "engine/scene/Scene.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/DefaultScene.h"
#include "engine/render/SoftwareRenderer.h"
#include "engine/brush/BrushMap.h"
#include "engine/core/Paths.h"
#include "engine/core/Log.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

using namespace fw;

namespace {

struct Options {
    std::string scenePath;
    std::string outPath;
    int width = 1280;
    int height = 720;
    std::string camera = "scene";
    bool havePos = false, haveLook = false, haveYaw = false, havePitch = false;
    vec3 pos{0.0f}, look{0.0f};
    float yaw = 0.0f, pitch = 0.0f, fov = 60.0f;
    bool wireframe = false;
    bool noGrid = false;
    bool bake = false;       // run the lightmap baker first (static lights only)
};

bool ParseVec3(const char* text, vec3& out) {
    return std::sscanf(text, "%f,%f,%f", &out.x, &out.y, &out.z) == 3;
}

void PrintUsage() {
    std::printf(
        "fwshot - render a Forgeworks scene to a PNG (CPU reference renderer)\n\n"
        "Usage: fwshot <scene.fwscene> <out.png> [options]\n"
        "  --width <px>        image width (default 1280)\n"
        "  --height <px>       image height (default 720)\n"
        "  --camera <mode>     scene | top | front | side | persp (default scene)\n"
        "  --pos x,y,z         camera position\n"
        "  --look x,y,z        camera look-at target\n"
        "  --yaw/--pitch <deg> explicit orientation (overrides --look)\n"
        "  --fov <deg>         field of view (default 60)\n"
        "  --wireframe         wireframe shading\n"
        "  --no-grid           hide the ground grid\n"
        "  --bake              bake lightmaps into assets/generated before rendering\n");
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const char*& out) { if (i + 1 < argc) out = argv[++i]; };
        if (a == "--width") { const char* v; next(v); opt.width = std::atoi(v); }
        else if (a == "--height") { const char* v; next(v); opt.height = std::atoi(v); }
        else if (a == "--camera") { const char* v; next(v); opt.camera = v; }
        else if (a == "--pos") { const char* v; next(v); opt.havePos = ParseVec3(v, opt.pos); }
        else if (a == "--look") { const char* v; next(v); opt.haveLook = ParseVec3(v, opt.look); }
        else if (a == "--yaw") { const char* v; next(v); opt.yaw = (float)std::atof(v); opt.haveYaw = true; }
        else if (a == "--pitch") { const char* v; next(v); opt.pitch = (float)std::atof(v); opt.havePitch = true; }
        else if (a == "--fov") { const char* v; next(v); opt.fov = (float)std::atof(v); }
        else if (a == "--wireframe") opt.wireframe = true;
        else if (a == "--no-grid") opt.noGrid = true;
        else if (a == "--bake") opt.bake = true;
        else if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
        else if (!a.empty() && a[0] != '-') positional.push_back(a);
        else { std::printf("Unknown option: %s\n", a.c_str()); PrintUsage(); return 2; }
    }

    if (positional.size() < 2) { PrintUsage(); return 2; }
    opt.scenePath = positional[0];
    opt.outPath = positional[1];
    if (opt.width <= 0) opt.width = 1280;
    if (opt.height <= 0) opt.height = 720;

    Paths::Initialize();
    FW_LOG_INFO("Rendering '%s' -> '%s' (%dx%d)", opt.scenePath.c_str(), opt.outPath.c_str(), opt.width, opt.height);

    // Convenience: if the requested scene doesn't exist yet, generate the
    // starter scene there (this is how assets/scenes/default.fwscene and its
    // materials/scripts come into existence on a fresh clone).
    if (!std::filesystem::exists(Paths::Resolve(opt.scenePath))) {
        FW_LOG_INFO("'%s' does not exist yet - generating the starter scene there", opt.scenePath.c_str());
        DefaultScene::EnsureStarterContent(opt.scenePath);
    }

    Scene scene("Screenshot");
    BrushMap brushes;
    if (!SceneSerializer::Load(scene, opt.scenePath, &brushes)) {
        std::printf("error: could not load scene '%s'\n", opt.scenePath.c_str());
        return 1;
    }
    if (Entity primaryCamera = scene.PrimaryCamera()) {
        auto& tc = primaryCamera.Get<TransformComponent>();
        auto& cc = primaryCamera.Get<CameraComponent>();
        if (!opt.havePos && !opt.haveLook && !opt.haveYaw && !opt.havePitch && opt.camera == "scene") {
            opt.pos = tc.local.position;
            const vec3 euler = tc.local.EulerDegrees();
            opt.pitch = euler.x;
            opt.yaw = euler.y;
            opt.fov = cc.fovDeg;
            opt.havePos = opt.haveYaw = opt.havePitch = true;
        }
    }

    // Preset viewpoints when the user didn't supply one.
    const float aspect = (float)opt.width / (float)opt.height;
    if (opt.camera == "top" && !opt.havePos) { opt.pos = vec3(0.0f, 24.0f, 0.01f); opt.pitch = -89.5f; opt.yaw = 0.0f; opt.havePos = opt.haveYaw = opt.havePitch = true; }
    else if (opt.camera == "front" && !opt.havePos) { opt.pos = vec3(0.0f, 3.0f, 16.0f); opt.pitch = -4.0f; opt.yaw = 0.0f; opt.havePos = opt.haveYaw = opt.havePitch = true; }
    // +90, not -90: from +X looking back at the origin the yaw is positive
    // under MakeCamera's atan2(-dx, -dz) convention. The negative value faced
    // the camera out into empty space and rendered nothing at all.
    else if (opt.camera == "side" && !opt.havePos) { opt.pos = vec3(16.0f, 3.0f, 0.0f); opt.pitch = -8.0f; opt.yaw = 90.0f; opt.havePos = opt.haveYaw = opt.havePitch = true; }
    // NOTE: yaw is +37.6, not -36. MakeCamera measures yaw as atan2(-dx, -dz),
    // so from (10,7,13) the angle back towards the origin is POSITIVE. The old
    // negative value swung the camera around to face away from the scene, which
    // is why this preset used to render 3 triangles instead of ~550.
    else if (opt.camera == "persp" && !opt.havePos) { opt.pos = vec3(10.0f, 7.0f, 13.0f); opt.pitch = -21.6f; opt.yaw = 37.6f; opt.havePos = opt.haveYaw = opt.havePitch = true; }

    if (!opt.havePos) { opt.pos = vec3(9.0f, 5.5f, 11.0f); opt.havePos = true; }
    if (opt.haveLook) {
        const vec3 d = glm::normalize(opt.look - opt.pos);
        opt.yaw = Degrees(std::atan2(-d.x, -d.z));
        opt.pitch = Degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
        opt.haveYaw = opt.havePitch = true;
    }

    RenderCamera camera = MakeCamera(opt.pos, opt.haveYaw ? opt.yaw : 0.0f, opt.havePitch ? opt.pitch : 0.0f,
                                     opt.fov, aspect, 0.05f, 2000.0f);

    RenderSettings settings;
    settings.drawGrid = !opt.noGrid;
    settings.wireframe = opt.wireframe;
    settings.gridCellSize = 1.0f;
    auto skyView = scene.Registry().view<SkyLightComponent>();
    if (!skyView.empty()) {
        auto& sky = scene.Registry().get<SkyLightComponent>(skyView.front());
        settings.ambientColor = sky.ambientColor;
        settings.ambientIntensity = sky.ambientIntensity;
    }

    SoftwareImage image;
    image.Resize(opt.width, opt.height);
    SoftwareRenderStats stats;
    SoftwareRenderer::RenderScene(scene, camera, settings, image, &stats);

    std::printf("%d mesh entities, %d lights, %d triangles drawn (%d back-face culled), %.1f%% geometry coverage\n",
                stats.meshEntities, stats.lightCount, stats.drawnTriangles, stats.culledTriangles,
                stats.GeometryCoverage() * 100.0f);
    if (stats.GeometryCoverage() <= 0.0f)
        std::printf("warning: nothing was rendered - check the camera position and the scene contents\n");

    std::error_code ec;
    if (std::filesystem::path(opt.outPath).has_parent_path())
        std::filesystem::create_directories(std::filesystem::path(opt.outPath).parent_path(), ec);

    if (!stbi_write_png(opt.outPath.c_str(), image.width, image.height, 4, image.Data(), image.width * 4)) {
        std::printf("error: could not write '%s'\n", opt.outPath.c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", opt.outPath.c_str(), image.width, image.height);
    return 0;
}
