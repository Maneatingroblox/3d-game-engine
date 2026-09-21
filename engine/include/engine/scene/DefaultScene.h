#pragma once
// The starter scene.
//
// The map maker used to open on a scene containing nothing but a camera and a
// light: no geometry, no grid, no sky - i.e. a blank viewport that is
// indistinguishable from a broken renderer. DefaultScene builds (and, on first
// run, saves) a small but complete level so there is always something visible
// to grab, move and play with.
//
// Used by:
//   * the editor at startup / File > New Scene,
//   * the game runtime when it has no scene to load,
//   * engine_tests (which renders it through the CPU renderer and asserts the
//     viewport is not blank).

#include "engine/core/Base.h"
#include <string>

namespace fw {

class Scene;
class BrushMap;

namespace DefaultScene {

// Clears `scene` and populates it with the starter level: a camera, a
// directional sun, a sky light, a ground brush/mesh and a few primitives to
// move around. `brushMap` (optional) receives matching Hammer brushes so the
// level can also be edited in Hammer mode.
void Build(Scene& scene, BrushMap* brushMap = nullptr);

// Creates assets/scenes/default.fwscene (and assets/materials/*.fwmat) on disk
// if the scene file is missing, so File > Open has something to open and the
// editor's default scene path actually exists. Returns true if the file exists
// afterwards. Never overwrites an existing scene.
bool EnsureStarterContent(const std::string& scenePath = "assets/scenes/default.fwscene");

// Path of the scene the editor opens on startup.
std::string DefaultScenePath();

} // namespace DefaultScene
} // namespace fw
