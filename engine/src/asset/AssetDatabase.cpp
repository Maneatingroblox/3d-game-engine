#include "engine/asset/AssetDatabase.h"
#include "engine/core/Log.h"
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
    if (!fs::exists(rootDir)) return;
    for (auto& entry : fs::recursive_directory_iterator(rootDir)) {
        if (!entry.is_regular_file()) continue;
        AssetEntry ae;
        ae.path = entry.path().generic_string();
        ae.type = ClassifyByExtension(ae.path);
        ae.lastWriteTime = entry.last_write_time();
        m_Entries.push_back(ae);
        m_KnownTimes[ae.path] = ae.lastWriteTime;
    }
    FW_LOG_INFO("AssetDatabase scanned %s: %zu asset(s)", rootDir.c_str(), m_Entries.size());
}

void AssetDatabase::PollForChanges(const std::function<void(const AssetEntry&)>& onChanged) {
    for (auto& entry : m_Entries) {
        if (!fs::exists(entry.path)) continue;
        auto currentTime = fs::last_write_time(entry.path);
        auto it = m_KnownTimes.find(entry.path);
        if (it == m_KnownTimes.end() || it->second != currentTime) {
            m_KnownTimes[entry.path] = currentTime;
            entry.lastWriteTime = currentTime;
            if (onChanged) onChanged(entry);
        }
    }
}

} // namespace fw
