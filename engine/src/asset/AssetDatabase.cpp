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

// Entries are stored project-relative ("assets/...") so paths are stable and
// match what MeshRendererComponent/ScriptComponent store and what
// Paths::Resolve() understands. Absolute paths outside the project root are
// kept absolute.
static std::string PortablePath(const fs::path& p) {
    std::error_code ec;
    const fs::path root = fs::path(Paths::ProjectRoot());
    const fs::path canonical = fs::weakly_canonical(p, ec);
    const fs::path base = fs::weakly_canonical(root, ec);
    if (!ec && !base.empty()) {
        const fs::path rel = canonical.lexically_relative(base);
        const std::string relStr = rel.generic_string();
        if (!relStr.empty() && relStr.rfind("..", 0) != 0) {
            return relStr;
        }
    }
    return canonical.empty() ? p.generic_string() : canonical.generic_string();
}

void AssetDatabase::Scan(const std::string& rootDir) {
    m_Entries.clear();
    m_KnownTimes.clear();
    const std::string resolvedRoot = Paths::Resolve(rootDir);
    std::error_code ec;
    if (!fs::exists(resolvedRoot, ec)) {
        FW_LOG_WARN("AssetDatabase: no such directory '%s' (resolved '%s')", rootDir.c_str(), resolvedRoot.c_str());
        return;
    }
    for (auto& entry : fs::recursive_directory_iterator(resolvedRoot, ec)) {
        if (!entry.is_regular_file()) continue;
        AssetEntry ae;
        ae.path = PortablePath(entry.path());
        ae.type = ClassifyByExtension(ae.path);
        ae.lastWriteTime = entry.last_write_time();
        m_Entries.push_back(ae);
        m_KnownTimes[ae.path] = ae.lastWriteTime;
    }
    std::sort(m_Entries.begin(), m_Entries.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.path < b.path; });
    FW_LOG_INFO("AssetDatabase scanned %s: %zu asset(s)", resolvedRoot.c_str(), m_Entries.size());
}

void AssetDatabase::PollForChanges(const std::function<void(const AssetEntry&)>& onChanged) {
    for (auto& entry : m_Entries) {
        const std::string resolved = Paths::Resolve(entry.path);
        std::error_code ec;
        if (!fs::exists(resolved, ec)) continue;
        auto currentTime = fs::last_write_time(resolved, ec);
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
