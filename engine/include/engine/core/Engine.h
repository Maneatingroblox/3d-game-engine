#pragma once
// Top-level engine context: owns the Scene, PhysicsWorld/PhysicsSystem,
// ScriptEngine, AudioEngine and (in non-headless builds) the Renderer, and
// drives the frame loop: fixed-step physics, script Update/FixedUpdate,
// transform propagation, then rendering. Both the editor's Play mode and the
// standalone game runtime are built on top of this class so behaviour is
// identical in both ("what you see in the editor's Play button is what you
// get in the shipped game").

#include "engine/core/Base.h"
#include "engine/scene/Scene.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/PhysicsSystem.h"
#include "engine/script/ScriptEngine.h"
#include "engine/brush/BrushMap.h"

#if !FORGEWORKS_HEADLESS
namespace fw { class AudioEngine; class Renderer; class Window; }
#endif

namespace fw {

enum class EngineRunState { Editing, Playing, Paused };

class Engine {
public:
    Engine();
    ~Engine();

    // headless=true skips renderer/audio device creation (used by
    // engine_tests and any offline tooling like batch lightmap baking).
    void Init(bool headless);
    void Shutdown();

    bool LoadScene(const std::string& path);
    bool SaveScene(const std::string& path);
    void NewScene(const std::string& name = "Untitled");

    // Enters Play mode: snapshots the edit-time scene, spawns physics bodies,
    // runs OnStart on every script.
    void Play();
    // Leaves Play mode: restores the edit-time scene snapshot.
    void Stop();
    void SetPaused(bool paused) { m_State = paused ? EngineRunState::Paused : EngineRunState::Playing; }

    // Advances simulation by `dt` seconds (called once per host frame).
    // Internally subdivides into fixed physics/script ticks.
    void Tick(float dt);

    Scene& GetScene() { return m_Scene; }
    BrushMap& GetBrushMap() { return m_BrushMap; }
    PhysicsWorld& GetPhysicsWorld() { return m_PhysicsWorld; }
    ScriptEngine& GetScriptEngine() { return m_ScriptEngine; }
    EngineRunState State() const { return m_State; }

#if !FORGEWORKS_HEADLESS
    AudioEngine* GetAudioEngine() { return m_Audio.get(); }
    Renderer* GetRenderer() { return m_Renderer.get(); }
#endif

private:
    Scene m_Scene;
    Scene m_EditSceneBackup;
    BrushMap m_BrushMap;
    PhysicsWorld m_PhysicsWorld;
    Scope<PhysicsSystem> m_PhysicsSystem;
    ScriptEngine m_ScriptEngine;
    EngineRunState m_State = EngineRunState::Editing;
    bool m_Headless = true;
    float m_FixedAccumulator = 0.0f;

#if !FORGEWORKS_HEADLESS
    Scope<AudioEngine> m_Audio;
    Scope<Renderer> m_Renderer;
#endif
};

} // namespace fw
