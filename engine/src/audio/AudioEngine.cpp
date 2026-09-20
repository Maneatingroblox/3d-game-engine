#include "engine/audio/AudioEngine.h"
#include "engine/core/Log.h"
#include <algorithm>

#define MA_NO_WEBAUDIO
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace fw {

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { Shutdown(); }

void AudioEngine::Init() {
    m_Engine = MakeScope<ma_engine>();
    ma_result result = ma_engine_init(nullptr, m_Engine.get());
    if (result != MA_SUCCESS) {
        FW_LOG_ERROR("Failed to initialize miniaudio engine (error %d)", (int)result);
        m_Engine.reset();
        return;
    }
    m_Initialized = true;
    FW_LOG_INFO("AudioEngine initialized (miniaudio)");
}

void AudioEngine::Shutdown() {
    if (!m_Initialized) return;
    m_EntitySounds.clear();
    m_OneShots.clear();
    if (m_Music) { ma_sound_uninit(m_Music.get()); m_Music.reset(); }
    if (m_Engine) ma_engine_uninit(m_Engine.get());
    m_Engine.reset();
    m_Initialized = false;
}

void AudioEngine::Update(Scene& scene, float dt) {
    FW_UNUSED(dt);
    if (!m_Initialized) return;

    // Listener position/orientation from the primary AudioListenerComponent
    // (falls back to the primary camera if none is marked).
    bool foundListener = false;
    scene.Each<TransformComponent, AudioListenerComponent>([&](Entity, TransformComponent& tc, AudioListenerComponent& al) {
        if (foundListener || !al.isPrimary) return;
        vec3 pos = vec3(tc.worldMatrix[3]);
        vec3 fwd = tc.local.Forward();
        vec3 up = tc.local.Up();
        ma_engine_listener_set_position(m_Engine.get(), 0, pos.x, pos.y, pos.z);
        ma_engine_listener_set_direction(m_Engine.get(), 0, fwd.x, fwd.y, fwd.z);
        ma_engine_listener_set_world_up(m_Engine.get(), 0, up.x, up.y, up.z);
        foundListener = true;
    });

    // Sync each AudioSourceComponent: create/destroy underlying ma_sound as
    // needed, keep position updated for 3D sources, honor play-on-start/loop.
    scene.Each<TransformComponent, AudioSourceComponent>([&](Entity e, TransformComponent& tc, AudioSourceComponent& src) {
        u64 key = (u64)e.Handle();
        auto it = m_EntitySounds.find(key);
        if (it == m_EntitySounds.end()) {
            if (src.soundAsset.empty()) return;
            ManagedSound ms;
            ms.sound = MakeScope<ma_sound>();
            ma_uint32 flags = src.is3D ? MA_SOUND_FLAG_DECODE : (MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION);
            if (ma_sound_init_from_file(m_Engine.get(), src.soundAsset.c_str(), flags, nullptr, nullptr, ms.sound.get()) != MA_SUCCESS) {
                FW_LOG_ERROR("Failed to load sound: %s", src.soundAsset.c_str());
                return;
            }
            ma_sound_set_looping(ms.sound.get(), src.loop);
            ma_sound_set_volume(ms.sound.get(), src.volume);
            ma_sound_set_pitch(ms.sound.get(), src.pitch);
            if (src.is3D) {
                ma_sound_set_min_distance(ms.sound.get(), src.minDistance);
                ma_sound_set_max_distance(ms.sound.get(), src.maxDistance);
            }
            it = m_EntitySounds.emplace(key, std::move(ms)).first;
            if (src.playOnStart) { ma_sound_start(it->second.sound.get()); it->second.playing = true; }
        }

        if (src.is3D) {
            vec3 pos = vec3(tc.worldMatrix[3]);
            ma_sound_set_position(it->second.sound.get(), pos.x, pos.y, pos.z);
        }
    });
}

void AudioEngine::Play(Entity entity) {
    auto it = m_EntitySounds.find((u64)entity.Handle());
    if (it != m_EntitySounds.end()) { ma_sound_start(it->second.sound.get()); it->second.playing = true; }
}

void AudioEngine::Stop(Entity entity) {
    auto it = m_EntitySounds.find((u64)entity.Handle());
    if (it != m_EntitySounds.end()) { ma_sound_stop(it->second.sound.get()); it->second.playing = false; }
}

void AudioEngine::PlayOneShot(const std::string& soundAsset, const vec3& worldPosition, float volume) {
    if (!m_Initialized) return;
    // Prune finished one-shots first.
    m_OneShots.erase(std::remove_if(m_OneShots.begin(), m_OneShots.end(), [](const Scope<ma_sound>& s) {
        return !ma_sound_is_playing(s.get());
    }), m_OneShots.end());

    auto sound = MakeScope<ma_sound>();
    if (ma_sound_init_from_file(m_Engine.get(), soundAsset.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
        FW_LOG_ERROR("Failed to play one-shot sound: %s", soundAsset.c_str());
        return;
    }
    ma_sound_set_position(sound.get(), worldPosition.x, worldPosition.y, worldPosition.z);
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_start(sound.get());
    m_OneShots.push_back(std::move(sound));
}

void AudioEngine::PlayMusic(const std::string& musicAsset, float volume, bool loop) {
    if (!m_Initialized) return;
    StopMusic();
    m_Music = MakeScope<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_STREAM;
    if (ma_sound_init_from_file(m_Engine.get(), musicAsset.c_str(), flags, nullptr, nullptr, m_Music.get()) != MA_SUCCESS) {
        FW_LOG_ERROR("Failed to play music: %s", musicAsset.c_str());
        m_Music.reset();
        return;
    }
    ma_sound_set_looping(m_Music.get(), loop);
    ma_sound_set_volume(m_Music.get(), volume);
    ma_sound_start(m_Music.get());
}

void AudioEngine::StopMusic() {
    if (m_Music) {
        ma_sound_stop(m_Music.get());
        ma_sound_uninit(m_Music.get());
        m_Music.reset();
    }
}

void AudioEngine::SetMasterVolume(float v) {
    if (m_Initialized) ma_engine_set_volume(m_Engine.get(), v);
}

} // namespace fw
