#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#if FW_PLATFORM_WINDOWS
#include <windows.h>
#include <string>
#include <vector>

namespace {

struct CommandLine {
    std::string screenshotPath;   // --screenshot <file.png>
    std::string projectRoot;      // --root <dir>
    std::string scenePath;        // --scene <file.fwscene>
    int frames = 12;              // --frames <n> (let ImGui settle before capture)
    bool help = false;
};

std::vector<std::string> SplitArgs(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (!inQuotes && (c == ' ' || c == '\t')) {
            if (!current.empty()) { out.push_back(current); current.clear(); }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

CommandLine ParseCommandLine() {
    CommandLine cl;
    std::vector<std::string> args = SplitArgs(GetCommandLineA());
    for (size_t i = 1; i < args.size(); i++) {
        const std::string& a = args[i];
        auto next = [&](std::string& out) {
            if (i + 1 < args.size()) out = args[++i];
        };
        if (a == "--screenshot" || a == "-s") next(cl.screenshotPath);
        else if (a == "--root") next(cl.projectRoot);
        else if (a == "--scene") next(cl.scenePath);
        else if (a == "--frames") { std::string v; next(v); if (!v.empty()) cl.frames = std::max(1, std::atoi(v.c_str())); }
        else if (a == "--help" || a == "-h" || a == "/?") cl.help = true;
    }
    return cl;
}

void PrintUsage() {
    const char* usage =
        "Forgeworks Map Maker\n"
        "\n"
        "Usage: ForgeworksEditor.exe [options]\n"
        "  --scene <file.fwscene>   Scene to open (default: assets/scenes/default.fwscene)\n"
        "  --root <dir>             Project root containing assets/ (default: auto-detected)\n"
        "  --screenshot <file.png>  Render a few frames, save the whole editor window\n"
        "                           to a PNG and exit (useful for CI / bug reports)\n"
        "  --frames <n>             Frames to render before the screenshot (default 12)\n"
        "  -h, --help               Show this message\n";
    MessageBoxA(nullptr, usage, "Forgeworks Map Maker", MB_OK | MB_ICONINFORMATION);
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const CommandLine cl = ParseCommandLine();
    if (cl.help) { PrintUsage(); return 0; }

    if (!cl.projectRoot.empty()) fw::Paths::SetProjectRootOverride(cl.projectRoot);
    fw::Paths::Initialize();

    fw::EditorApp app;
    if (!cl.scenePath.empty()) app.SetStartupScene(cl.scenePath);

    if (!cl.screenshotPath.empty()) {
        FW_LOG_INFO("Screenshot mode: rendering %d frame(s) then writing %s", cl.frames, cl.screenshotPath.c_str());
        const bool ok = app.RunWithScreenshot("Forgeworks Map Maker", 1920, 1080, cl.screenshotPath, cl.frames, true);
        return ok ? 0 : 1;
    }

    app.Run("Forgeworks Map Maker", 1920, 1080, true);
    return 0;
}
#else
int main() { return 0; }
#endif
