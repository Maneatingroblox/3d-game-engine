#include "engine/core/Paths.h"
#include "engine/core/Log.h"

#include <filesystem>
#include <cstdlib>

#if FW_PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

namespace fs = std::filesystem;

namespace fw {

namespace {

std::string g_ExecutableDir;
std::string g_ProjectRoot;
std::string g_RootSource;
std::string g_Override;
bool g_FoundRoot = false;
bool g_Initialized = false;

bool IsAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p.size() >= 2 && p[1] == ':') return true;             // Windows drive
    if (p[0] == '/' || p[0] == '\\') return true;              // POSIX / UNC-ish
    return false;
}

// Walks `start` and its parents looking for a directory that contains an
// `assets` subdirectory (the project root marker).
bool FindRootUpwards(const fs::path& start, fs::path& outRoot) {
    std::error_code ec;
    if (start.empty()) return false;
    fs::path dir = fs::weakly_canonical(start, ec);
    if (ec) dir = start;

    for (int level = 0; level < 8 && !dir.empty(); level++) {
        if (fs::is_directory(dir / "assets", ec) && !ec) {
            outRoot = dir;
            return true;
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}

std::string QueryExecutableDir() {
#if FW_PLATFORM_WINDOWS
    wchar_t buffer[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(nullptr, buffer, (DWORD)(MAX_PATH * 4));
    if (len == 0 || len >= MAX_PATH * 4) return {};
    // Let std::filesystem do the wide -> native-narrow conversion (it handles
    // non-ASCII install paths correctly, unlike a naive iterator copy).
    return fs::path(std::wstring(buffer, len)).parent_path().string();
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) return {};
    buffer[len] = '\0';
    return fs::path(buffer).parent_path().string();
#endif
}

} // namespace

void Paths::Initialize(const std::string& executablePath) {
    if (g_Initialized) return;
    g_Initialized = true;

    if (!executablePath.empty()) {
        g_ExecutableDir = fs::path(executablePath).parent_path().string();
    } else {
        g_ExecutableDir = QueryExecutableDir();
    }

    std::error_code ec;

    // 1) explicit override
    if (!g_Override.empty()) {
        fs::path overridePath = g_Override;
        if (fs::is_directory(overridePath / "assets", ec) && !ec) {
            g_ProjectRoot = fs::weakly_canonical(overridePath, ec).string();
            g_RootSource = "explicit override";
            g_FoundRoot = true;
            return;
        }
    }

    // 2) environment variable
    if (const char* envRoot = std::getenv("FORGEWORKS_ROOT")) {
        fs::path root;
        if (FindRootUpwards(envRoot, root)) {
            g_ProjectRoot = fs::weakly_canonical(root, ec).string();
            g_RootSource = "FORGEWORKS_ROOT";
            g_FoundRoot = true;
            return;
        }
    }

    // 3) next to the executable, walking up (build/bin -> repo root)
    {
        fs::path root;
        if (FindRootUpwards(fs::current_path(ec), root)) {
            g_ProjectRoot = root.string();
            g_RootSource = "working directory";
            g_FoundRoot = true;
            return;
        }
    }

    // 4) executable directory, walking up
    if (!g_ExecutableDir.empty()) {
        fs::path root;
        if (FindRootUpwards(g_ExecutableDir, root)) {
            g_ProjectRoot = root.string();
            g_RootSource = "executable location";
            g_FoundRoot = true;
            return;
        }
    }

    // Nothing found: keep the historical behaviour of using the working
    // directory, and log loudly so the missing content is diagnosable.
    g_ProjectRoot = fs::current_path(ec).string();
    g_RootSource = "fallback (no assets/ directory found)";
    g_FoundRoot = false;
    FW_LOG_WARN("Paths: could not find a project root containing 'assets/'. "
                "Set FORGEWORKS_ROOT or pass --root <dir>. Falling back to '%s'.",
                g_ProjectRoot.c_str());
}

void Paths::SetProjectRootOverride(const std::string& dir) {
    g_Override = dir;
    g_Initialized = false;
    g_FoundRoot = false;
}

const std::string& Paths::ExecutableDir() {
    if (!g_Initialized) Initialize();
    return g_ExecutableDir;
}

const std::string& Paths::ProjectRoot() {
    if (!g_Initialized) Initialize();
    return g_ProjectRoot;
}

std::string Paths::AssetRoot() {
    if (!g_Initialized) Initialize();
    if (g_ProjectRoot.empty()) return "assets";
    return (fs::path(g_ProjectRoot) / "assets").string();
}

std::string Paths::Resolve(const std::string& path) {
    if (path.empty()) return path;
    if (!g_Initialized) Initialize();
    if (IsAbsolutePath(path)) return path;
    if (g_ProjectRoot.empty()) return path;

    std::error_code ec;
    // A path that is valid relative to the working directory is respected
    // as-is: callers that pass real, existing relative paths (tools, tests,
    // save dialogs) keep working exactly as before.
    if (fs::exists(path, ec) && !ec) return path;

    fs::path relative(path);
    if (relative.begin() != relative.end()) {
        const std::string first = relative.begin()->string();
        if (first == "assets") {
            fs::path candidate = fs::path(g_ProjectRoot) / relative;
            if (fs::exists(candidate, ec) && !ec) return candidate.string();
            return candidate.string(); // report the path we *expected*
        }
    }

    fs::path candidate = fs::path(g_ProjectRoot) / relative;
    if (fs::exists(candidate, ec) && !ec) return candidate.string();

    // Unknown path: prefer the project-root-relative form when a root was
    // found, otherwise leave it untouched so error messages stay meaningful.
    return g_FoundRoot ? candidate.string() : path;
}

std::string Paths::Asset(const std::string& relativeToAssets) {
    if (!g_Initialized) Initialize();
    if (relativeToAssets.empty()) return relativeToAssets;
    return Resolve("assets/" + relativeToAssets);
}

bool Paths::FoundProjectRoot() {
    if (!g_Initialized) Initialize();
    return g_FoundRoot;
}

const std::string& Paths::RootSource() {
    if (!g_Initialized) Initialize();
    return g_RootSource;
}

} // namespace fw
