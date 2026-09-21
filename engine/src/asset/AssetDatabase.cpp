#include "engine/asset/AssetDatabase.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <algorithm>

namespace fs = std::filesystem;

namespace fw {

AssetType AssetDatabase::ClassifyByExtension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".fwmesh" || ext == ".obj" || ext == ".fbx" || ext == ".gltf") return AssetType::Mesh;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return AssetType::Texture;
    if (ext == ".fwmat") return AssetType::Material;
    if (ext == ".lua") return AssetType::Script;
    if (ext == ".fwscene") return AssetType::Scene;
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") return AssetType::Sound;
    if (ext == ".ttf" || ext == ".otf") return AssetType::Font;
    if (ext == ".hlsl" || ext == ".fx") return AssetType::Shader;
    return AssetType::Unknown;
}

void AssetDatabase::Scan(const std::string& rootDir) {
    m_Entries.clear();
    m_KnownTimes.clear();

    // `rootDir` is normally the project-relative "assets", which only exists as
    // a relative path when the process was started from the repository root.
    // Resolve it so the browser is populated however the editor was launched.
    const std::string resolvedRoot = Paths::Resolve(rootDir);
    std::error_code ec;
    if (!fs::exists(resolvedRoot, ec)) {
        FW_LOG_WARN("AssetDatabase: '%s' does not exist (resolved to '%s')", rootDir.c_str(), resolvedRoot.c_str());
        return;
    }

    // Entries keep the project-relative path ("assets/scripts/spin.lua"): that
    // is what scenes/components store and what the UI should show. The absolute
    // location is recovered on demand via Paths::Resolve().
    const fs::path projectRoot(Paths::ProjectRoot());

    for (auto it = fs::recursive_directory_iterator(resolvedRoot, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;

        AssetEntry ae;
        const fs::path full = it->path();
        std::error_code relEc;
        const fs::path rel = projectRoot.empty() ? full : fs::relative(full, projectRoot, relEc);
        ae.path = (relEc || rel.empty() || rel.native().rfind("..", 0) == 0)
                      ? full.generic_string()
                      : rel.generic_string();
        ae.type = ClassifyByExtension(ae.path);
        ae.lastWriteTime = it->last_write_time(ec);
        m_KnownTimes[ae.path] = ae.lastWriteTime;
        m_Entries.push_back(std::move(ae));
    }

    std::sort(m_Entries.begin(), m_Entries.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.path < b.path; });

    FW_LOG_INFO("AssetDatabase scanned %s: %zu asset(s)", resolvedRoot.c_str(), m_Entries.size());
}

void AssetDatabase::PollForChanges(const std::function<void(const AssetEntry&)>& onChanged) {
    std::error_code ec;
    for (auto& entry : m_Entries) {
        const std::string resolved = Paths::Resolve(entry.path);
        if (!fs::exists(resolved, ec)) continue;
        const auto currentTime = fs::last_write_time(resolved, ec);
        if (ec) continue;
        auto it = m_KnownTimes.find(entry.path);
        if (it == m_KnownTimes.end() || it->second != currentTime) {
            m_KnownTimes[entry.path] = currentTime;
            entry.lastWriteTime = currentTime;
            if (onChanged) onChanged(entry);
        }
    }
}

} // namespace fw
