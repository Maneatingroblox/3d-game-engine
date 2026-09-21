#pragma once
// The scripting layer. This is what makes Forgeworks behave "like a game
// engine" rather than a fixed demo: every entity with a ScriptComponent gets
// a Lua environment created from its .lua file, and the engine calls into
// well-known functions on it every frame:
//
//   function OnStart(self) ... end
//   function OnUpdate(self, dt) ... end
//   function OnFixedUpdate(self, dt) ... end
//   function OnDestroy(self) ... end
//   function OnCollisionEnter(self, other) ... end
//   function OnCollisionExit(self, other) ... end
//   function OnTriggerEnter(self, other) ... end
//   function OnTriggerExit(self, other) ... end
//   function OnKeyDown(self, key) ... end / OnKeyUp ...
//
// `self` exposes .entity, .transform, .Properties (exported vars set in the
// inspector) plus engine bindings under global tables: Input, Physics,
// Audio, Time, Scene, Vector3, Quaternion, Log, Events.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <entt/entt.hpp>
#include <sol/forward.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace fw {

class Scene;
class PhysicsWorld;
class AudioEngine;
struct ContactEvent;

// Forward-declared opaque wrapper so headers that only need to pass a Lua
// state around don't have to include the whole of sol2.
struct LuaStateHandle;

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    void Init(Scene* scene, PhysicsWorld* physics, AudioEngine* audio);
    void Shutdown();

    // Called once when entering Play mode: instantiates every ScriptComponent
    // in the scene and calls OnStart on each.
    void StartAll();
    // Called once when leaving Play mode.
    void StopAll();

    void Update(float dt);
    void FixedUpdate(float dt);

    void DispatchContactEvents(const std::vector<ContactEvent>& events);
    // Invokes OnCollisionEnter/Exit or OnTriggerEnter/Exit on `self` for
    // entity `a`, passing entity `b`'s handle as the `other` argument.
    void DispatchCollision(entt::entity a, entt::entity b, bool isTrigger, bool isStart);
    void DispatchKeyDown(int key);
    void DispatchKeyUp(int key);
    // Mouse state mirrored into the Lua `Input` table (fed by
    // engine/platform/Input when it is bound via Input::BindScriptEngine).
    void DispatchMouseMove(const vec2& pos, const vec2& delta);
    void DispatchMouseButton(int button, bool down);
    void DispatchMouseWheel(float delta);
    // Clears per-frame edge state (pressed/released/wheel/delta) at frame start.
    void BeginInputFrame();

    // (Re)loads and instantiates the script for a single entity; used by the
    // editor's "Attach Script" and hot-reload-on-save.
    bool AttachScript(entt::entity entity, const std::string& scriptPath);
    void DetachScript(entt::entity entity);

    // Reloads the given script file's source for every entity that uses it
    // (hot reload while the game/editor is running).
    void ReloadScript(const std::string& scriptPath);

    bool HasErrors() const { return !m_LastErrors.empty(); }
    const std::vector<std::string>& Errors() const { return m_LastErrors; }
    void ClearErrors() { m_LastErrors.clear(); }

    // Access to the raw Lua state for advanced editor tooling (e.g. an
    // in-editor Lua console / REPL).
    sol::state* Lua() { return m_Lua.get(); }

private:
    struct ScriptInstance;
    void BindAPI();
    void BindEntityHandle(entt::entity e, sol::table& selfTable);
    void ReportError(const std::string& scriptPath, const std::string& what);

    Scope<sol::state> m_Lua;
    Scene* m_Scene = nullptr;
    PhysicsWorld* m_Physics = nullptr;
    AudioEngine* m_Audio = nullptr;

    std::unordered_map<u64, Scope<ScriptInstance>> m_Instances; // key: entt::entity as integral
    std::vector<std::string> m_LastErrors;
    bool m_Running = false;
};

} // namespace fw
