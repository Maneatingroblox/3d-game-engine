#include "engine/core/Settings.h"
#include "engine/core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace fw {

Settings& Settings::Get() {
    static Settings instance;
    return instance;
}

void Settings::ResetToDefaults() {
    *this = Settings();
    controls.keyBindings = {
        {"MoveForward", 'W'}, {"MoveBack", 'S'}, {"MoveLeft", 'A'}, {"MoveRight", 'D'},
        {"Jump", 32 /*VK_SPACE*/}, {"Sprint", 16 /*VK_SHIFT*/}, {"Interact", 'E'},
        {"Pause", 27 /*VK_ESCAPE*/},
    };
}

bool Settings::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) { ResetToDefaults(); return false; }
    json j;
    try { f >> j; } catch (...) { ResetToDefaults(); return false; }

    if (j.contains("video")) {
        auto& jv = j["video"];
        video.resolutionWidth = jv.value("resolutionWidth", 1920);
        video.resolutionHeight = jv.value("resolutionHeight", 1080);
        video.windowMode = (WindowMode)jv.value("windowMode", 0);
        video.vsync = jv.value("vsync", true);
        video.maxFPS = jv.value("maxFPS", 0);
        video.renderScale = jv.value("renderScale", 1.0f);
        video.msaaSamples = jv.value("msaaSamples", 4);
        video.bloom = jv.value("bloom", true);
        video.ssao = jv.value("ssao", false);
        video.fov = jv.value("fov", 75.0f);
        video.gamma = jv.value("gamma", 2.2f);
    }
    if (j.contains("audio")) {
        auto& ja = j["audio"];
        audio.masterVolume = ja.value("masterVolume", 1.0f);
        audio.musicVolume = ja.value("musicVolume", 0.8f);
        audio.sfxVolume = ja.value("sfxVolume", 1.0f);
        audio.voiceVolume = ja.value("voiceVolume", 1.0f);
        audio.muteOnFocusLoss = ja.value("muteOnFocusLoss", true);
    }
    if (j.contains("controls")) {
        auto& jc = j["controls"];
        controls.mouseSensitivity = jc.value("mouseSensitivity", 1.0f);
        controls.invertY = jc.value("invertY", false);
        if (jc.contains("keyBindings")) {
            controls.keyBindings.clear();
            for (auto& [k, v] : jc["keyBindings"].items()) controls.keyBindings[k] = v.get<int>();
        }
    }
    if (controls.keyBindings.empty()) ResetToDefaults();
    FW_LOG_INFO("Settings loaded from %s", path.c_str());
    return true;
}

bool Settings::Save(const std::string& path) const {
    json j;
    j["video"] = {
        {"resolutionWidth", video.resolutionWidth}, {"resolutionHeight", video.resolutionHeight},
        {"windowMode", (int)video.windowMode}, {"vsync", video.vsync}, {"maxFPS", video.maxFPS},
        {"renderScale", video.renderScale}, {"msaaSamples", video.msaaSamples},
        {"bloom", video.bloom}, {"ssao", video.ssao}, {"fov", video.fov}, {"gamma", video.gamma},
    };
    j["audio"] = {
        {"masterVolume", audio.masterVolume}, {"musicVolume", audio.musicVolume},
        {"sfxVolume", audio.sfxVolume}, {"voiceVolume", audio.voiceVolume},
        {"muteOnFocusLoss", audio.muteOnFocusLoss},
    };
    json keyBindings = json::object();
    for (auto& [k, v] : controls.keyBindings) keyBindings[k] = v;
    j["controls"] = {
        {"mouseSensitivity", controls.mouseSensitivity}, {"invertY", controls.invertY},
        {"keyBindings", keyBindings},
    };

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return true;
}

} // namespace fw
