#pragma once
// Lightweight asset indexing: scans the assets/ directory, classifies files
// by extension, and tracks last-write-time so the editor can hot-reload
// scripts/textures/meshes when they change on disk (like Godot's filesystem
// dock + auto reimport).

#include "engine/core/Base.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <functional>

namespace fw {

enum class AssetType { Unknown, Mesh, Texture, Material, Script, Scene, Sound, Font, Shader };

struct AssetEntry {
    std::string path;
    AssetType type = AssetType::Unknown;
    std::filesystem::file_time_type lastWriteTime{};
};

class AssetDatabase {
public:
    void Scan(const std::string& rootDir);
    const std::vector<AssetEntry>& Entries() const { return m_Entries; }

    static AssetType ClassifyByExtension(const std::string& path);

    // Polls the filesystem for changed files (call periodically, e.g. once a
    // second in the editor) and invokes `onChanged` for each modified asset -
    // this is what powers Lua script hot-reload while Play mode is running.
    void PollForChanges(const std::function<void(const AssetEntry&)>& onChanged);

private:
    std::vector<AssetEntry> m_Entries;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_KnownTimes;
};

} // namespace fw
