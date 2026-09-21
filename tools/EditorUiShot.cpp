// fwui - render the Map Maker's real user interface to a PNG, without a GPU,
// without a window and without a display.
//
// This runs the *actual* EditorApp UI code (EditorApp::OnImGui: menu bar,
// toolbar, dock layout, viewport, hierarchy, inspector, asset browser, console,
// script editor, Hammer panels) inside an ImGui context that has no platform or
// renderer backend, rasterizes the resulting draw lists on the CPU
// (engine/render/SoftCanvas.h) and writes a PNG.
//
// Two things come out of that:
//   1. docs/screenshots/editor_*.png - what the editor actually looks like,
//      produced from the real code, so "the editor is blank" can be checked and
//      reviewed without a Windows box.
//   2. A report of what was drawn (draw lists, vertices, index buffers and the
//      fraction of the frame covered by UI pixels). If a change ever makes the
//      interface stop drawing, this prints it instead of shipping an empty
//      window.
//
// Usage: fwui [--out <file.png>] [--width N] [--height N] [--frames N]
//             [--scene <file.fwscene>] [--root <dir>] [--no-viewport-png]
//             [--game] [--play]

#include "editor/EditorApp.h"
#include "game/GameApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/render/SoftCanvas.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

// Application's run loop is Windows-only; the UI itself is not. Expose the
// protected lifecycle hooks so this tool can drive the real editor code.
class HeadlessEditor : public fw::EditorApp {
public:
    using fw::EditorApp::OnInit;
    using fw::EditorApp::OnImGui;
    using fw::EditorApp::OnShutdown;
    using fw::EditorApp::OnScreenshot;
    using fw::EditorApp::OnUpdate;
};

// ---------------------------------------------------------------------------
// Game variants: the same Application/SoftCanvas plumbing, but with GameApp's
// runtime shell on top (main menu, pause menu, scene through the CPU renderer).
// ---------------------------------------------------------------------------
class HeadlessGame : public fw::GameApp {
public:
    using fw::GameApp::OnImGui;
    using fw::GameApp::OnInit;
    using fw::GameApp::OnShutdown;
    using fw::GameApp::OnSoftwareRender;
    using fw::GameApp::OnUpdate;
};

struct Options {
    std::string out = "docs/screenshots/editor_ui.png";
    std::string scene;
    std::string root;
    int width = 1600;
    int height = 900;
    int frames = 10;
    bool viewportPng = true;
    bool game = false;   // --game: render the game runtime instead of the editor
    bool play = false;   // --play: start the scene instead of showing the menu
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](std::string& dst) { if (i + 1 < argc) dst = argv[++i]; };
        if (a == "--out" || a == "-o") next(o.out);
        else if (a == "--scene") next(o.scene);
        else if (a == "--root") next(o.root);
        else if (a == "--width") { std::string v; next(v); o.width = std::atoi(v.c_str()); }
        else if (a == "--height") { std::string v; next(v); o.height = std::atoi(v.c_str()); }
        else if (a == "--frames") { std::string v; next(v); o.frames = std::atoi(v.c_str()); }
        else if (a == "--no-viewport-png") o.viewportPng = false;
        else if (a == "--game") o.game = true;
        else if (a == "--play") o.play = true;
    }
    if (o.width < 320) o.width = 320;
    if (o.height < 240) o.height = 240;
    if (o.frames < 1) o.frames = 1;
    return o;
}

// Pixels that differ from the canvas background: how much of the frame the UI
// actually covers. A blank editor scores ~0.
struct FrameStats {
    int drawLists = 0;
    int vertices = 0;
    int indices = 0;
    int uiPixels = 0;
    int totalPixels = 0;
    int distinctColors = 0;

    float Coverage() const { return totalPixels > 0 ? (float)uiPixels / (float)totalPixels : 0.0f; }
};

FrameStats Analyze(const fw::SoftCanvas& canvas) {
    FrameStats stats;
    if (ImDrawData* dd = ImGui::GetDrawData()) {
        stats.drawLists = dd->CmdListsCount;
        stats.vertices = dd->TotalVtxCount;
        stats.indices = dd->TotalIdxCount;
    }
    const int w = canvas.Width(), h = canvas.Height();
    stats.totalPixels = w * h;
    const fw::u8* p = canvas.Pixels();
    // 5 bits per channel = 32768 buckets; keep this in step with the shifts below.
    bool seen[1 << 15] = {};
    for (int i = 0; i < w * h; i++) {
        const fw::u8* px = p + (size_t)i * 4;
        if (px[0] != 13 || px[1] != 13 || px[2] != 16) stats.uiPixels++;
        const int key = ((px[0] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[2] >> 3);
        seen[key] = true;
    }
    for (bool b : seen) if (b) stats.distinctColors++;
    return stats;
}

bool WritePng(const std::string& path, const fw::SoftCanvas& canvas) {
    const std::string resolved = fw::Paths::Resolve(path);
    std::error_code ec;
    if (std::filesystem::path(resolved).has_parent_path())
        std::filesystem::create_directories(std::filesystem::path(resolved).parent_path(), ec);
    if (!stbi_write_png(resolved.c_str(), canvas.Width(), canvas.Height(), 4,
                        canvas.Pixels(), canvas.Pitch())) {
        std::fprintf(stderr, "fwui: failed to write %s\n", resolved.c_str());
        return false;
    }
    std::printf("fwui: wrote %s (%dx%d)\n", resolved.c_str(), canvas.Width(), canvas.Height());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = ParseArgs(argc, argv);

    if (!opt.root.empty()) fw::Paths::SetProjectRootOverride(opt.root);
    fw::Paths::Initialize();

    // The apps run on the CPU presentation path: no D3D11 device, no swap chain.
    // Their content, their interface and the capture all go through SoftCanvas -
    // the exact code the executables use on a Windows machine when --software is
    // passed or the GPU path is unavailable.
    HeadlessEditor editor;
    HeadlessGame game;
    fw::Application& appBase = opt.game ? static_cast<fw::Application&>(game)
                                        : static_cast<fw::Application&>(editor);
    if (opt.game) {
        game.SetSoftwareMode(true);
        game.SetStartupReportFrame(0);
        if (opt.play) game.SetStartPlaying(true);
    } else {
        editor.SetSoftwareMode(true);
        editor.SetViewportPreviewScale(1.0f);
        editor.SetStartupReportFrame(0);
    }
    appBase.Canvas().Resize(opt.width, opt.height);
    if (!opt.scene.empty()) {
        if (opt.game) game.SetStartupScene(opt.scene);
        else editor.SetStartupScene(opt.scene);
    }

    // ImGui with no platform/renderer backend: the core library, the docking
    // branch and SoftCanvas are all a headless tool needs.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2((float)opt.width, (float)opt.height);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendPlatformName = "headless";
    io.BackendRendererName = "forgeworks_softcanvas";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // SoftCanvas creates the font atlas
    io.IniFilename = nullptr;  // ignore any imgui.ini: capture the built-in default layout
    ImGui::StyleColorsDark();

    const bool started = opt.game ? game.OnInit() : editor.OnInit();
    if (!started) {
        std::fprintf(stderr, "fwui: %s::OnInit() failed - see the log above\n",
                     opt.game ? "GameApp" : "EditorApp");
        ImGui::DestroyContext();
        return 1;
    }

    FrameStats stats;
    for (int frame = 0; frame < opt.frames; frame++) {
        io.DisplaySize = ImVec2((float)opt.width, (float)opt.height);
        // Exactly what Application::MainLoop does on the CPU presentation path:
        // clear -> the app's own scene -> ImGui interface -> rasterize.
        if (opt.game) game.OnUpdate(io.DeltaTime);
        else editor.OnUpdate(io.DeltaTime);   // ticks the engine, marks the CPU viewport dirty
        appBase.Canvas().Clear(IM_COL32(13, 13, 16, 255));
        if (opt.game) game.OnSoftwareRender(appBase.Canvas());
        ImGui::NewFrame();
        if (opt.game) game.OnImGui();          // the real game UI (menu / HUD)
        else editor.OnImGui();                 // the real editor interface
        ImGui::Render();
        appBase.Canvas().RenderImGuiFrame();
        stats = Analyze(appBase.Canvas());
    }

    std::printf("fwui: %s | draw lists %d | vertices %d | indices %d | UI coverage %.1f%% | distinct colours %d\n",
                opt.game ? "game" : "editor",
                stats.drawLists, stats.vertices, stats.indices, stats.Coverage() * 100.0f, stats.distinctColors);

    bool ok = WritePng(opt.out, appBase.Canvas());
    if (opt.viewportPng && !opt.game) {
        // Also save the viewport on its own (the CPU renderer at full size).
        editor.OnScreenshot(opt.out);
    }

    if (opt.game) game.OnShutdown();
    else editor.OnShutdown();
    ImGui::DestroyContext();

    if (stats.vertices == 0 || stats.uiPixels == 0) {
        std::fprintf(stderr, "fwui: the %s interface produced no visible pixels\n",
                     opt.game ? "game" : "editor");
        return 2;
    }
    return ok ? 0 : 1;
}
