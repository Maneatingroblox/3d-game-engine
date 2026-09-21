#include "game/GameApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#if FW_PLATFORM_WINDOWS
#include <windows.h>
#include <string>
#include <vector>

namespace {

struct CommandLine {
    std::string scenePath;      // --scene <file.fwscene>
    std::string screenshotPath; // --screenshot <file.png>
    std::string projectRoot;    // --root <dir>
    bool autoplay = false;      // --play (skip the main menu)
    int frames = 12;
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
        auto next = [&](std::string& out) { if (i + 1 < args.size()) out = args[++i]; };
        if (a == "--scene") next(cl.scenePath);
        else if (a == "--screenshot" || a == "-s") next(cl.screenshotPath);
        else if (a == "--root") next(cl.projectRoot);
        else if (a == "--frames") { std::string v; next(v); if (!v.empty()) cl.frames = std::max(1, std::atoi(v.c_str())); }
        else if (a == "--play") cl.autoplay = true;
    }
    return cl;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const CommandLine cl = ParseCommandLine();
    if (!cl.projectRoot.empty()) fw::Paths::SetProjectRootOverride(cl.projectRoot);
    fw::Paths::Initialize();

    fw::GameApp app;
    if (!cl.scenePath.empty()) app.SetStartupScene(cl.scenePath);
    if (cl.autoplay) app.SetStartPlaying(true);

    if (!cl.screenshotPath.empty()) {
        FW_LOG_INFO("Screenshot mode: rendering %d frame(s) then writing %s", cl.frames, cl.screenshotPath.c_str());
        const bool ok = app.RunWithScreenshot("Forgeworks", 1600, 900, cl.screenshotPath, cl.frames, false);
        return ok ? 0 : 1;
    }

    app.Run("Forgeworks", 1600, 900);
    return 0;
}
#else
int main() { return 0; }
#endif
