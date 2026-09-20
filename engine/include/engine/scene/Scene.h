#pragma once
// Scene: owns the EnTT registry, entity lifetime, hierarchy operations and
// scene-wide state (physics world, name lookup). Both the editor and the game
// runtime operate on a Scene; the editor also keeps an "edit-time" copy that
// it restores when the user presses Stop after Play-testing.

#include "engine/core/Base.h"
#include "engine/core/UUID.h"
#include "engine/ecs/Components.h"
#include <entt/entt.hpp>
#include <string>
#include <unordered_map>

namespace fw {

class PhysicsWorld;
class ScriptEngine;
class AudioEngine;

class Entity {
public:
    Entity() = default;
    Entity(entt::entity handle, entt::registry* registry) : m_Handle(handle), m_Registry(registry) {}

    template <typename T, typename... Args>
    T& AddOrReplace(Args&&... args) {
        return m_Registry->emplace_or_replace<T>(m_Handle, std::forward<Args>(args)...);
    }
    template <typename T> bool Has() const { return m_Registry->all_of<T>(m_Handle); }
    template <typename T> T& Get() const { return m_Registry->get<T>(m_Handle); }
    template <typename T> T* TryGet() const { return m_Registry->try_get<T>(m_Handle); }
    template <typename T> void Remove() { m_Registry->remove<T>(m_Handle); }

    UUID Id() const { return Get<IDComponent>().id; }
    const std::string& Name() const { return Get<NameComponent>().name; }
    void SetName(const std::string& n) { Get<NameComponent>().name = n; }

    entt::entity Handle() const { return m_Handle; }
    bool IsValid() const { return m_Registry && m_Registry->valid(m_Handle); }
    explicit operator bool() const { return IsValid(); }
    bool operator==(const Entity& o) const { return m_Handle == o.m_Handle && m_Registry == o.m_Registry; }

private:
    entt::entity m_Handle = entt::null;
    entt::registry* m_Registry = nullptr;
};

class Scene {
public:
    Scene(const std::string& name = "Untitled");
    ~Scene();

    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    Entity CreateEntity(const std::string& name = "Entity");
    Entity CreateEntityWithId(UUID id, const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    void SetParent(Entity child, Entity parent);
    void Unparent(Entity child);

    Entity FindByUUID(UUID id);
    Entity FindByName(const std::string& name);
    Entity PrimaryCamera();

    // Recompute all world matrices from the hierarchy (call once per frame
    // before rendering / physics sync).
    void UpdateTransforms();

    entt::registry& Registry() { return m_Registry; }
    const std::string& Name() const { return m_Name; }
    void SetName(const std::string& n) { m_Name = n; }

    // Deep-copies the whole registry; used by the editor to snapshot/restore
    // scene state around Play mode.
    Scene Clone() const;

    template <typename... Components, typename Fn>
    void Each(Fn&& fn) {
        auto view = m_Registry.view<Components...>();
        for (auto e : view) fn(Entity(e, &m_Registry), view.template get<Components>(e)...);
    }

private:
    void UpdateTransformRecursive(entt::entity e, const mat4& parentWorld);

    std::string m_Name;
    entt::registry m_Registry;
    std::unordered_map<u64, entt::entity> m_UUIDToEntity;
};

} // namespace fw
