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
    std::string logPath;          // --log <file.log>
    int frames = 12;              // --frames <n> (let ImGui settle before capture)
    bool console = false;         // --console: show a console window with the log
    bool software = false;        // --software: CPU presentation path (no D3D11 at all)
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
        else if (a == "--log") next(cl.logPath);
        else if (a == "--console") cl.console = true;
        else if (a == "--software") cl.software = true;
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
        "  --console                Attach a console window and echo the log to it\n"
        "  --software               Do not use Direct3D at all: render the whole editor\n"
        "                           (viewport + interface) on the CPU and blit it to the\n"
        "                           window with GDI. Use this on a machine with a broken\n"
        "                           or missing GPU/driver; the editor also falls back to\n"
        "                           this path automatically when D3D11 cannot start.\n"
        "  --log <file.log>         Write the log here (default: <exe dir>\\forgeworks.log)\n"
        "  -h, --help               Show this message\n"
        "\n"
        "The editor always writes a log file next to the executable: if the window\n"
        "looks empty or the process exits immediately, read that file first - it\n"
        "records the project root, window/swap chain sizes, shader compilation, the\n"
        "first-frame report and any D3D11 error. If start-up fails, the reason and\n"
        "the log tail are also shown inside the window itself.\n";
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
    if (!cl.logPath.empty()) app.SetLogFile(cl.logPath);
    if (cl.console) app.SetConsoleOutput(true);
    if (cl.software) app.SetSoftwareMode(true);

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
