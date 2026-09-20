#pragma once
// Bridges the ECS (RigidBodyComponent/ColliderComponent/CharacterController)
// with the low-level PhysicsWorld (Jolt wrapper), and forwards collision /
// trigger events into the ScriptEngine as Lua callbacks. This is the system
// that makes "attach a RigidBody + Collider in the inspector" actually
// simulate.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <entt/entt.hpp>
#include <unordered_map>

namespace fw {

class Scene;
class PhysicsWorld;
class ScriptEngine;

class PhysicsSystem {
public:
    PhysicsSystem(Scene* scene, PhysicsWorld* world);

    // Creates Jolt bodies for every entity with RigidBody+Collider that
    // doesn't have one yet, and characters for CharacterController entities.
    // Called once when entering Play mode (and incrementally for spawned entities).
    void SyncNewBodies();
    // Removes bodies whose entity no longer exists / lost its components.
    void RemoveOrphanBodies();

    // Advances physics and writes simulation results (position/rotation, or
    // character position) back into TransformComponent.
    void Step(float dt, ScriptEngine* scriptEngine);

    void Clear();

private:
    entt::entity EntityForBody(u32 bodyId) const;

    Scene* m_Scene;
    PhysicsWorld* m_World;
    std::unordered_map<u32, entt::entity> m_BodyToEntity;
    std::unordered_map<u32, entt::entity> m_CharacterToEntity;
};

} // namespace fw
