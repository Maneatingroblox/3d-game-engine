#pragma once
// Project/asset path resolution.
//
// Every content path in Forgeworks is written relative to the repository root
// ("assets/scenes/default.fwscene", "assets/shaders/Mesh.hlsl", ...). That is
// only correct when the process' current working directory happens to be the
// repo root - which is true when you launch from a developer prompt, but NOT
// when you double-click ForgeworksEditor.exe inside build/bin, or launch it
// from a shortcut/solution explorer.
//
// When the working directory is wrong, everything content-related silently
// fails: the HLSL shaders never compile (the renderer's passes EarlyOut), the
// scene file is "missing" so the editor opens an empty scene, and the asset
// browser is blank. The visible symptom is an editor window that shows nothing.
//
// Paths::Initialize() fixes that by locating the project root once, at
// startup, from (in order): an explicit override, the FORGEWORKS_ROOT
// environment variable, the directory containing the executable (walking up
// from it), and finally the current working directory (walking up from it).
// All content loads then go through Paths::Resolve().

#include "engine/core/Base.h"
#include <string>

namespace fw {

class Paths {
public:
    // Locates the project root (the directory that contains `assets/`).
    // Safe to call multiple times; the first successful result wins.
    // `executablePath` is optional - when empty the platform's own
    // "path of the running binary" query is used.
    static void Initialize(const std::string& executablePath = "");

    // Explicit override, e.g. from a command line argument. Empty clears it.
    static void SetProjectRootOverride(const std::string& dir);

    // Directory that contains the running executable ("" if unknown).
    static const std::string& ExecutableDir();

    // Directory that contains `assets/`. Never empty after Initialize();
    // falls back to the current working directory.
    static const std::string& ProjectRoot();

    // `<ProjectRoot>/assets` (falls back to the relative "assets" only when
    // no root could be determined at all).
    static std::string AssetRoot();

    // Resolves a content path:
    //   ""                  -> ""
    //   absolute path       -> unchanged
    //   "assets/foo/bar"    -> "<ProjectRoot>/assets/foo/bar"
    //   "foo/bar"           -> "<ProjectRoot>/foo/bar"
    // Paths that exist relative to the working directory are returned as-is,
    // so explicitly-passed relative paths keep working.
    static std::string Resolve(const std::string& path);

    // Same as Resolve(), specialised for asset-relative paths that are given
    // without the leading "assets/" (e.g. "scenes/default.fwscene").
    static std::string Asset(const std::string& relativeToAssets);

    // True when Initialize() actually found a directory containing `assets/`.
    static bool FoundProjectRoot();

    // Human readable description of how the root was determined (logging).
    static const std::string& RootSource();
};

} // namespace fw
