#pragma once
// Persistent user settings (video/audio/controls/gameplay), backed by
// settings.json next to the executable. Used by both the editor and the
// standalone game's Settings menu.

#include "engine/core/Base.h"
#include <string>
#include <unordered_map>

namespace fw {

enum class WindowMode { Windowed, Borderless, Fullscreen };

struct VideoSettings {
    int resolutionWidth = 1920;
    int resolutionHeight = 1080;
    WindowMode windowMode = WindowMode::Windowed;
    bool vsync = true;
    int maxFPS = 0; // 0 = unlimited
    float renderScale = 1.0f;
    int msaaSamples = 4;
    bool bloom = true;
    bool ssao = false;
    float fov = 75.0f;
    float gamma = 2.2f;
};

struct AudioSettings {
    float masterVolume = 1.0f;
    float musicVolume = 0.8f;
    float sfxVolume = 1.0f;
    float voiceVolume = 1.0f;
    bool muteOnFocusLoss = true;
};

struct ControlSettings {
    float mouseSensitivity = 1.0f;
    bool invertY = false;
    std::unordered_map<std::string, int> keyBindings; // action name -> virtual key code
};

struct Settings {
    VideoSettings video;
    AudioSettings audio;
    ControlSettings controls;

    static Settings& Get();
    bool Load(const std::string& path = "settings.json");
    bool Save(const std::string& path = "settings.json") const;
    void ResetToDefaults();
};

} // namespace fw
