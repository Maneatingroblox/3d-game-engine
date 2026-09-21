#include "engine/script/ScriptEngine.h"
#include "engine/scene/Scene.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include <sol/sol.hpp>
#include <fstream>
#include <sstream>
#include <chrono>

#if !FORGEWORKS_HEADLESS
#include "engine/audio/AudioEngine.h"
#endif

namespace fw {

struct ScriptEngine::ScriptInstance {
    entt::entity entity;
    std::string scriptPath;
    sol::table self;      // the `self` table passed to every callback
    sol::table properties; // exported properties (self.Properties)
    bool started = false;
    bool valid = false;
};

static std::string ReadFile(const std::string& path) {
    // Script paths are project-relative ("assets/scripts/foo.lua"); resolve
    // against the project root so they load no matter what the working
    // directory is (a double-clicked exe starts in an arbitrary directory).
    std::ifstream f(Paths::Resolve(path), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

ScriptEngine::ScriptEngine() = default;
ScriptEngine::~ScriptEngine() { Shutdown(); }

static float g_TimeSinceStart = 0.0f;
static float g_DeltaTime = 0.0f;

void ScriptEngine::Init(Scene* scene, PhysicsWorld* physics, AudioEngine* audio) {
    m_Scene = scene;
    m_Physics = physics;
    m_Audio = audio;

    m_Lua = MakeScope<sol::state>();
    m_Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                           sol::lib::string, sol::lib::os, sol::lib::io, sol::lib::coroutine);
    BindAPI();
    FW_LOG_INFO("ScriptEngine initialized (Lua %s)", LUA_RELEASE);
}

void ScriptEngine::Shutdown() {
    StopAll();
    m_Instances.clear();
    m_Lua.reset();
}

// ---------------------------------------------------------------------------
// API binding: math types, Log, Input (stub hooked by platform), Physics,
// Audio, Scene queries, and the Entity handle userdata.
// ---------------------------------------------------------------------------
struct LuaVec3Bridge {
    static vec3 Make(float x, float y, float z) { return vec3(x, y, z); }
};

// Simple global keyboard state the platform layer / editor updates each frame.
struct InputState {
    std::unordered_map<int, bool> down;
    std::unordered_map<int, bool> pressedThisFrame;
    std::unordered_map<int, bool> releasedThisFrame;
    vec2 mouseDelta{0.0f};
    vec2 mousePos{0.0f};
    float wheelDelta = 0.0f;
    bool mouseButtons[8] = {false};
    bool mousePressed[8] = {false};
    bool mouseReleased[8] = {false};
};
static InputState g_Input;

void ScriptEngine::BindAPI() {
    sol::state& lua = *m_Lua;

    // ---- Vector3 ----
    lua.new_usertype<vec3>("Vector3",
        sol::constructors<vec3(), vec3(float, float, float)>(),
        "x", &vec3::x, "y", &vec3::y, "z", &vec3::z,
        sol::meta_function::addition, [](const vec3& a, const vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const vec3& a, const vec3& b) { return a - b; },
        sol::meta_function::multiplication, sol::overload(
            [](const vec3& a, float s) { return a * s; },
            [](float s, const vec3& a) { return a * s; }),
        sol::meta_function::to_string, [](const vec3& v) { return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; },
        "length", [](const vec3& v) { return glm::length(v); },
        "normalized", [](const vec3& v) { return glm::length(v) > 1e-8f ? glm::normalize(v) : v; },
        "dot", [](const vec3& a, const vec3& b) { return glm::dot(a, b); },
        "cross", [](const vec3& a, const vec3& b) { return glm::cross(a, b); }
    );
    lua["Vector3"]["zero"] = vec3(0.0f);
    lua["Vector3"]["one"] = vec3(1.0f);
    lua["Vector3"]["up"] = vec3(0, 1, 0);
    lua["Vector3"]["forward"] = vec3(0, 0, -1);
    lua["Vector3"]["right"] = vec3(1, 0, 0);

    // ---- Quaternion ----
    lua.new_usertype<quat>("Quaternion",
        sol::constructors<quat(), quat(float, float, float, float)>(),
        "x", &quat::x, "y", &quat::y, "z", &quat::z, "w", &quat::w,
        "euler_degrees", [](const quat& q) { return glm::degrees(glm::eulerAngles(q)); },
        sol::meta_function::multiplication, sol::overload(
            [](const quat& a, const quat& b) { return a * b; },
            [](const quat& a, const vec3& v) { return a * v; })
    );
    lua.set_function("QuatFromEuler", [](float xDeg, float yDeg, float zDeg) {
        return quat(glm::radians(vec3(xDeg, yDeg, zDeg)));
    });

    // ---- Log ----
    sol::table logTable = lua.create_named_table("Log");
    logTable.set_function("Info",  [](const std::string& s) { FW_LOG_INFO("[Lua] %s", s.c_str()); });
    logTable.set_function("Warn",  [](const std::string& s) { FW_LOG_WARN("[Lua] %s", s.c_str()); });
    logTable.set_function("Error", [](const std::string& s) { FW_LOG_ERROR("[Lua] %s", s.c_str()); });

    // ---- Time ----
    sol::table timeTable = lua.create_named_table("Time");
    timeTable.set_function("delta_time", []() { return g_DeltaTime; });
    timeTable.set_function("time_since_start", []() { return g_TimeSinceStart; });

    // ---- Input ----
    sol::table inputTable = lua.create_named_table("Input");
    inputTable.set_function("is_key_down", [](int key) { auto it = g_Input.down.find(key); return it != g_Input.down.end() && it->second; });
    inputTable.set_function("is_key_pressed", [](int key) { auto it = g_Input.pressedThisFrame.find(key); return it != g_Input.pressedThisFrame.end() && it->second; });
    inputTable.set_function("is_key_released", [](int key) { auto it = g_Input.releasedThisFrame.find(key); return it != g_Input.releasedThisFrame.end() && it->second; });
    inputTable.set_function("mouse_delta", []() { return g_Input.mouseDelta; });
    inputTable.set_function("mouse_position", []() { return g_Input.mousePos; });
    inputTable.set_function("mouse_wheel", []() { return g_Input.wheelDelta; });
    inputTable.set_function("is_mouse_button_down", [](int btn) { return btn >= 0 && btn < 8 ? g_Input.mouseButtons[btn] : false; });
    inputTable.set_function("is_mouse_button_pressed", [](int btn) { return btn >= 0 && btn < 8 ? g_Input.mousePressed[btn] : false; });
    inputTable.set_function("is_mouse_button_released", [](int btn) { return btn >= 0 && btn < 8 ? g_Input.mouseReleased[btn] : false; });

    // ---- Entity handle exposed to scripts ----
    lua.new_usertype<Entity>("EntityHandle",
        "get_position", [](Entity& e) { return e.Get<TransformComponent>().local.position; },
        "set_position", [](Entity& e, const vec3& p) { e.Get<TransformComponent>().local.position = p; },
        "get_rotation", [](Entity& e) { return e.Get<TransformComponent>().local.rotation; },
        "set_rotation", [](Entity& e, const quat& q) { e.Get<TransformComponent>().local.rotation = q; },
        "get_scale", [](Entity& e) { return e.Get<TransformComponent>().local.scale; },
        "set_scale", [](Entity& e, const vec3& s) { e.Get<TransformComponent>().local.scale = s; },
        "forward", [](Entity& e) { return e.Get<TransformComponent>().local.Forward(); },
        "right", [](Entity& e) { return e.Get<TransformComponent>().local.Right(); },
        "up", [](Entity& e) { return e.Get<TransformComponent>().local.Up(); },
        "name", [](Entity& e) { return e.Name(); },
        "set_name", [](Entity& e, const std::string& n) { e.SetName(n); },
        "is_valid", [](Entity& e) { return e.IsValid(); },
        "has_rigidbody", [](Entity& e) { return e.Has<RigidBodyComponent>(); }
    );

    // ---- Scene queries ----
    sol::table sceneTable = lua.create_named_table("Scene");
    sceneTable.set_function("find_entity", [this](const std::string& name) -> sol::object {
        if (!m_Scene) return sol::lua_nil;
        Entity e = m_Scene->FindByName(name);
        if (!e) return sol::lua_nil;
        return sol::make_object(*m_Lua, e);
    });
    sceneTable.set_function("spawn", [this](const std::string& name) -> sol::object {
        if (!m_Scene) return sol::lua_nil;
        Entity e = m_Scene->CreateEntity(name);
        return sol::make_object(*m_Lua, e);
    });
    sceneTable.set_function("destroy", [this](Entity e) {
        if (m_Scene) m_Scene->DestroyEntity(e);
    });

    // ---- Physics ----
    sol::table physicsTable = lua.create_named_table("Physics");
    physicsTable.set_function("raycast", [this](const vec3& origin, const vec3& dir, float maxDist) -> sol::table {
        sol::table t = m_Lua->create_table();
        if (!m_Physics) { t["hit"] = false; return t; }
        RaycastHit h = m_Physics->Raycast(origin, dir, maxDist);
        t["hit"] = h.hit;
        t["point"] = h.point;
        t["normal"] = h.normal;
        t["distance"] = h.distance;
        return t;
    });
    physicsTable.set_function("set_gravity", [this](const vec3& g) { if (m_Physics) m_Physics->SetGravity(g); });
    physicsTable.set_function("add_force", [this](Entity e, const vec3& force) {
        if (!m_Physics || !e.Has<RigidBodyComponent>()) return;
        m_Physics->AddForce(e.Get<RigidBodyComponent>().runtimeBodyId, force);
    });
    physicsTable.set_function("add_impulse", [this](Entity e, const vec3& impulse) {
        if (!m_Physics || !e.Has<RigidBodyComponent>()) return;
        m_Physics->AddImpulse(e.Get<RigidBodyComponent>().runtimeBodyId, impulse);
    });
    physicsTable.set_function("set_velocity", [this](Entity e, const vec3& v) {
        if (!m_Physics || !e.Has<RigidBodyComponent>()) return;
        vec3 ang; vec3 curLin;
        m_Physics->GetBodyVelocity(e.Get<RigidBodyComponent>().runtimeBodyId, curLin, ang);
        m_Physics->SetBodyVelocity(e.Get<RigidBodyComponent>().runtimeBodyId, v, ang);
    });
    physicsTable.set_function("get_velocity", [this](Entity e) -> vec3 {
        if (!m_Physics || !e.Has<RigidBodyComponent>()) return vec3(0.0f);
        vec3 lin, ang;
        m_Physics->GetBodyVelocity(e.Get<RigidBodyComponent>().runtimeBodyId, lin, ang);
        return lin;
    });

#if !FORGEWORKS_HEADLESS
    // ---- Audio ----
    sol::table audioTable = lua.create_named_table("Audio");
    audioTable.set_function("play", [this](Entity e) { if (m_Audio) m_Audio->Play(e); });
    audioTable.set_function("stop", [this](Entity e) { if (m_Audio) m_Audio->Stop(e); });
    audioTable.set_function("play_one_shot", [this](const std::string& asset, const vec3& pos, float volume) {
        if (m_Audio) m_Audio->PlayOneShot(asset, pos, volume);
    });
    audioTable.set_function("play_music", [this](const std::string& asset, float volume, bool loop) {
        if (m_Audio) m_Audio->PlayMusic(asset, volume, loop);
    });
#endif

    // ---- Events (simple pub/sub for cross-script communication) ----
    sol::table eventsTable = lua.create_named_table("Events");
    lua["__event_listeners"] = lua.create_table();
    lua.script(R"lua(
        function Events.subscribe(name, fn)
            if __event_listeners[name] == nil then __event_listeners[name] = {} end
            table.insert(__event_listeners[name], fn)
        end
        function Events.emit(name, ...)
            local listeners = __event_listeners[name]
            if listeners == nil then return end
            for _, fn in ipairs(listeners) do fn(...) end
        end
    )lua");
}

bool ScriptEngine::AttachScript(entt::entity entity, const std::string& scriptPath) {
    std::string src = ReadFile(scriptPath);
    if (src.empty()) {
        ReportError(scriptPath, "could not read file or file is empty");
        return false;
    }

    auto inst = MakeScope<ScriptInstance>();
    inst->entity = entity;
    inst->scriptPath = scriptPath;

    sol::environment env(*m_Lua, sol::create, m_Lua->globals());
    auto result = m_Lua->safe_script(src, env, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        ReportError(scriptPath, err.what());
        return false;
    }

    sol::table self = m_Lua->create_table();
    self["entity"] = Entity(entity, &m_Scene->Registry());
    self["Properties"] = m_Lua->create_table();
    self["__env"] = env;
    // Copy every global function this script defined (OnStart, OnUpdate, ...)
    // into `self` so `self:OnUpdate(dt)` style or `env.OnUpdate(self,dt)` both work.
    for (auto& kv : env) {
        if (kv.second.is<sol::function>()) self[kv.first] = kv.second;
    }

    inst->self = self;
    inst->valid = true;
    m_Instances[(u64)entity] = std::move(inst);
    return true;
}

void ScriptEngine::DetachScript(entt::entity entity) {
    auto it = m_Instances.find((u64)entity);
    if (it == m_Instances.end()) return;
    if (it->second->started) {
        sol::function onDestroy = it->second->self["OnDestroy"];
        if (onDestroy.valid()) {
            auto r = onDestroy(it->second->self);
            if (!r.valid()) { sol::error e = r; ReportError(it->second->scriptPath, e.what()); }
        }
    }
    m_Instances.erase(it);
}

void ScriptEngine::StartAll() {
    m_Running = true;
    g_TimeSinceStart = 0.0f;
    if (!m_Scene) return;
    m_Scene->Each<ScriptComponent>([this](Entity e, ScriptComponent& sc) {
        if (!sc.enabled || sc.scriptAsset.empty()) return;
        AttachScript(e.Handle(), sc.scriptAsset);
        auto it = m_Instances.find((u64)e.Handle());
        if (it != m_Instances.end()) {
            // Push exported properties from the component into self.Properties
            for (auto& [k, v] : sc.properties) it->second->self["Properties"][k] = v;
        }
    });
    for (auto& [id, inst] : m_Instances) {
        sol::function onStart = inst->self["OnStart"];
        if (onStart.valid()) {
            auto r = onStart(inst->self);
            if (!r.valid()) { sol::error e = r; ReportError(inst->scriptPath, e.what()); }
        }
        inst->started = true;
    }
}

void ScriptEngine::StopAll() {
    if (!m_Running) return;
    for (auto& [id, inst] : m_Instances) {
        if (!inst->started) continue;
        sol::function onDestroy = inst->self["OnDestroy"];
        if (onDestroy.valid()) {
            auto r = onDestroy(inst->self);
            if (!r.valid()) { sol::error e = r; ReportError(inst->scriptPath, e.what()); }
        }
    }
    m_Instances.clear();
    m_Running = false;
}

void ScriptEngine::Update(float dt) {
    g_DeltaTime = dt;
    g_TimeSinceStart += dt;
    for (auto& [id, inst] : m_Instances) {
        sol::function fn = inst->self["OnUpdate"];
        if (!fn.valid()) continue;
        auto r = fn(inst->self, dt);
        if (!r.valid()) { sol::error e = r; ReportError(inst->scriptPath, e.what()); }
    }
}

void ScriptEngine::FixedUpdate(float dt) {
    for (auto& [id, inst] : m_Instances) {
        sol::function fn = inst->self["OnFixedUpdate"];
        if (!fn.valid()) continue;
        auto r = fn(inst->self, dt);
        if (!r.valid()) { sol::error e = r; ReportError(inst->scriptPath, e.what()); }
    }
}

void ScriptEngine::DispatchContactEvents(const std::vector<ContactEvent>& events) {
    // Mapping physics body IDs back to entities is owned by PhysicsSystem,
    // which calls DispatchCollision() directly for each resolved pair.
    FW_UNUSED(events);
}

void ScriptEngine::DispatchCollision(entt::entity a, entt::entity b, bool isTrigger, bool isStart) {
    if (!m_Scene || !m_Scene->Registry().valid(a)) return;
    auto it = m_Instances.find((u64)a);
    if (it == m_Instances.end() || !it->second->started) return;

    const char* fnName = isTrigger
        ? (isStart ? "OnTriggerEnter" : "OnTriggerExit")
        : (isStart ? "OnCollisionEnter" : "OnCollisionExit");

    sol::function fn = it->second->self[fnName];
    if (!fn.valid()) return;
    Entity other(b, &m_Scene->Registry());
    auto r = fn(it->second->self, other);
    if (!r.valid()) { sol::error e = r; ReportError(it->second->scriptPath, e.what()); }
}

void ScriptEngine::DispatchKeyDown(int key) {
    g_Input.down[key] = true;
    g_Input.pressedThisFrame[key] = true;
    for (auto& [id, inst] : m_Instances) {
        sol::function fn = inst->self["OnKeyDown"];
        if (fn.valid()) fn(inst->self, key);
    }
}

void ScriptEngine::DispatchKeyUp(int key) {
    g_Input.down[key] = false;
    g_Input.releasedThisFrame[key] = true;
    for (auto& [id, inst] : m_Instances) {
        sol::function fn = inst->self["OnKeyUp"];
        if (fn.valid()) fn(inst->self, key);
    }
}

void ScriptEngine::DispatchMouseMove(const vec2& pos, const vec2& delta) {
    g_Input.mousePos = pos;
    g_Input.mouseDelta += delta;
}

void ScriptEngine::DispatchMouseButton(int button, bool down) {
    if (button < 0 || button >= 8) return;
    if (down && !g_Input.mouseButtons[button]) g_Input.mousePressed[button] = true;
    if (!down && g_Input.mouseButtons[button]) g_Input.mouseReleased[button] = true;
    g_Input.mouseButtons[button] = down;
}

void ScriptEngine::DispatchMouseWheel(float delta) {
    g_Input.wheelDelta += delta;
}

void ScriptEngine::BeginInputFrame() {
    g_Input.pressedThisFrame.clear();
    g_Input.releasedThisFrame.clear();
    g_Input.mouseDelta = vec2(0.0f);
    g_Input.wheelDelta = 0.0f;
    for (bool& p : g_Input.mousePressed) p = false;
    for (bool& r : g_Input.mouseReleased) r = false;
}

void ScriptEngine::ReloadScript(const std::string& scriptPath) {
    // Compare project-resolved forms so "assets/scripts/x.lua" and a resolved
    // absolute path to the same file reload the same instances.
    const std::string resolved = Paths::Resolve(scriptPath);
    std::vector<entt::entity> affected;
    for (auto& [id, inst] : m_Instances) {
        if (inst->scriptPath == scriptPath || Paths::Resolve(inst->scriptPath) == resolved)
            affected.push_back(inst->entity);
    }
    for (auto e : affected) {
        DetachScript(e);
        AttachScript(e, scriptPath);
        auto it = m_Instances.find((u64)e);
        if (it != m_Instances.end()) {
            sol::function onStart = it->second->self["OnStart"];
            if (onStart.valid()) onStart(it->second->self);
            it->second->started = true;
        }
    }
    FW_LOG_INFO("Hot-reloaded script: %s (%zu instance(s))", scriptPath.c_str(), affected.size());
}

void ScriptEngine::ReportError(const std::string& scriptPath, const std::string& what) {
    std::string msg = scriptPath + ": " + what;
    m_LastErrors.push_back(msg);
    FW_LOG_ERROR("[Script] %s", msg.c_str());
}

} // namespace fw
