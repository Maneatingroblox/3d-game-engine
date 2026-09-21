#include "engine/core/Engine.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/core/Log.h"
#include "engine/platform/Input.h"

#if !FORGEWORKS_HEADLESS
#include "engine/audio/AudioEngine.h"
#include "engine/render/Renderer.h"
#endif

namespace fw {

Engine::Engine() : m_Scene("Untitled") {}
Engine::~Engine() { Shutdown(); }

void Engine::Init(bool headless) {
    m_Headless = headless;
    m_PhysicsWorld.Init();
    m_PhysicsSystem = MakeScope<PhysicsSystem>(&m_Scene, &m_PhysicsWorld);

#if !FORGEWORKS_HEADLESS
    if (!headless) {
        m_Audio = MakeScope<AudioEngine>();
        m_Audio->Init();
    }
    m_ScriptEngine.Init(&m_Scene, &m_PhysicsWorld, m_Audio.get());
#else
    m_ScriptEngine.Init(&m_Scene, &m_PhysicsWorld, nullptr);
#endif

    // Feed gameplay scripts the same input stream the editor/game read, so
    // `Input.is_key_down(...)` etc. work inside Lua.
    Input::Get().BindScriptEngine(&m_ScriptEngine);

    FW_LOG_INFO("Engine initialized (headless=%s)", headless ? "true" : "false");
}

void Engine::Shutdown() {
    Stop();
    Input::Get().BindScriptEngine(nullptr);
    m_ScriptEngine.Shutdown();
#if !FORGEWORKS_HEADLESS
    if (m_Audio) m_Audio->Shutdown();
#endif
    m_PhysicsWorld.Shutdown();
}

bool Engine::LoadScene(const std::string& path) {
    Stop();
    m_Scene = Scene("Untitled");
    m_BrushMap = BrushMap();
    bool ok = SceneSerializer::Load(m_Scene, path, &m_BrushMap);
    if (ok) m_Scene.UpdateTransforms();
    return ok;
}

bool Engine::SaveScene(const std::string& path) {
    return SceneSerializer::Save(m_Scene, path, &m_BrushMap);
}

void Engine::NewScene(const std::string& name) {
    Stop();
    m_Scene = Scene(name);
    m_BrushMap = BrushMap();
}

void Engine::Play() {
    if (m_State != EngineRunState::Editing) return;
    m_EditSceneBackup = m_Scene.Clone();
    m_PhysicsSystem->Clear();
    m_PhysicsSystem->SyncNewBodies();
    m_ScriptEngine.StartAll();
    m_State = EngineRunState::Playing;
    m_FixedAccumulator = 0.0f;
    FW_LOG_INFO("Play mode started");
}

void Engine::Stop() {
    if (m_State == EngineRunState::Editing) return;
    m_ScriptEngine.StopAll();
    m_PhysicsSystem->Clear();
    m_Scene = m_EditSceneBackup.Clone();
    m_State = EngineRunState::Editing;
    FW_LOG_INFO("Play mode stopped, editor scene restored");
}

void Engine::Tick(float dt) {
    if (m_State == EngineRunState::Playing) {
        const float fixedStep = 1.0f / 60.0f;
        m_FixedAccumulator += dt;
        int iterations = 0;
        while (m_FixedAccumulator >= fixedStep && iterations < 8) {
            m_ScriptEngine.FixedUpdate(fixedStep);
            m_PhysicsSystem->Step(fixedStep, &m_ScriptEngine);
            m_FixedAccumulator -= fixedStep;
            iterations++;
        }
        m_ScriptEngine.Update(dt);
    }
    m_Scene.UpdateTransforms();

#if !FORGEWORKS_HEADLESS
    if (m_Audio) m_Audio->Update(m_Scene, dt);
#endif
}

} // namespace fw
