#pragma once
// Serializes/deserializes a Scene to the .fwscene JSON format. This is what
// the editor's Save/Open and the game runtime's "load level" use.

#include "engine/core/Base.h"
#include <string>

namespace fw {

class Scene;
class BrushMap;

class SceneSerializer {
public:
    static bool Save(const Scene& scene, const std::string& path, const BrushMap* brushMap = nullptr);
    static bool Load(Scene& scene, const std::string& path, BrushMap* brushMap = nullptr);
};

} // namespace fw
