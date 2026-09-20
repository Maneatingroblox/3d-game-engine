#pragma once
// 3D positional audio via miniaudio (WASAPI backend on Windows). Drives
// AudioSourceComponent/AudioListenerComponent every frame and exposes
// Play/Stop/PlayOneShot/PlayMusic to the Lua `Audio` API.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include "engine/scene/Scene.h"
#include <string>
#include <unordered_map>
#include <memory>

struct ma_engine;
struct ma_sound;

namespace fw {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void Init();
    void Shutdown();

    // Syncs playing/looping sounds and 3D positions from ECS every frame,
    // and updates the listener from the primary AudioListenerComponent.
    void Update(Scene& scene, float dt);

    void Play(Entity entity);
    void Stop(Entity entity);
    void PlayOneShot(const std::string& soundAsset, const vec3& worldPosition, float volume = 1.0f);
    void PlayMusic(const std::string& musicAsset, float volume = 1.0f, bool loop = true);
    void StopMusic();

    void SetMasterVolume(float v);

private:
    struct ManagedSound {
        Scope<ma_sound> sound;
        bool playing = false;
    };

    Scope<ma_engine> m_Engine;
    std::unordered_map<u64, ManagedSound> m_EntitySounds; // keyed by entt::entity integral
    std::vector<Scope<ma_sound>> m_OneShots; // fire-and-forget, garbage collected when finished
    Scope<ma_sound> m_Music;
    bool m_Initialized = false;
};

} // namespace fw
