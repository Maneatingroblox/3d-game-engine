#pragma once
// Thin, engine-friendly wrapper around Jolt Physics. Owns the PhysicsSystem,
// job/temp allocators and broadphase layer glue, and exposes a simple API the
// ECS physics system (and the Lua scripting API) can call: create/destroy
// bodies from Collider/RigidBody components, step the simulation, raycast,
// and pump collision/trigger events that the script engine turns into
// OnCollisionEnter/OnTriggerEnter callbacks.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <vector>
#include <functional>
#include <unordered_set>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class Body;
    class BodyInterface;
}

namespace fw {

enum class PhysicsLayer : u8 { Static = 0, Dynamic = 1, Trigger = 2 };

struct RaycastHit {
    bool hit = false;
    vec3 point{0.0f};
    vec3 normal{0.0f};
    float distance = 0.0f;
    u32 bodyId = 0xFFFFFFFF;
};

struct ContactEvent {
    u32 bodyA = 0xFFFFFFFF;
    u32 bodyB = 0xFFFFFFFF;
    bool isTrigger = false;
    bool isStart = true; // true = enter, false = exit
};

struct BodyCreateInfo {
    PhysicsLayer layer = PhysicsLayer::Static;
    enum class Shape { Box, Sphere, Capsule, ConvexHull, TriangleMesh } shape = Shape::Box;
    vec3 halfExtents{0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    vec3 position{0.0f};
    quat rotation{1, 0, 0, 0};
    float mass = 1.0f;
    float friction = 0.6f;
    float restitution = 0.1f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool isStatic = true;
    bool isKinematic = false;
    bool isTrigger = false;
    bool gravityEnabled = true;
    const std::vector<vec3>* meshVertices = nullptr; // for ConvexHull/TriangleMesh
    const std::vector<u32>* meshIndices = nullptr;
    void* userData = nullptr;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void Init();
    void Shutdown();

    // Advances the simulation by `dt` seconds using a fixed internal step
    // (default 1/60) — call once per fixed-update tick.
    void Step(float dt);

    u32 CreateBody(const BodyCreateInfo& info);
    void DestroyBody(u32 bodyId);

    void SetBodyTransform(u32 bodyId, const vec3& position, const quat& rotation);
    void GetBodyTransform(u32 bodyId, vec3& outPosition, quat& outRotation) const;
    void SetBodyVelocity(u32 bodyId, const vec3& linear, const vec3& angular);
    void GetBodyVelocity(u32 bodyId, vec3& outLinear, vec3& outAngular) const;
    void AddForce(u32 bodyId, const vec3& force);
    void AddImpulse(u32 bodyId, const vec3& impulse);
    void SetGravityEnabled(u32 bodyId, bool enabled);

    RaycastHit Raycast(const vec3& origin, const vec3& direction, float maxDistance);
    std::vector<RaycastHit> RaycastAll(const vec3& origin, const vec3& direction, float maxDistance);
    bool OverlapSphere(const vec3& center, float radius, std::vector<u32>& outBodies);

    void SetGravity(const vec3& g);
    vec3 Gravity() const { return m_Gravity; }

    // Character controller (simple kinematic capsule sweep-based mover)
    u32 CreateCharacter(const vec3& position, float radius, float height, float maxSlopeDeg);
    void DestroyCharacter(u32 characterId);
    // Moves a character by `displacement`, sliding along geometry; updates
    // grounded state and returns the resulting displacement actually applied.
    vec3 MoveCharacter(u32 characterId, const vec3& displacement, float dt);
    bool IsCharacterGrounded(u32 characterId) const;
    vec3 CharacterPosition(u32 characterId) const;
    void SetCharacterPosition(u32 characterId, const vec3& pos);

    // Collision/trigger events collected during Step(), drained once per
    // frame by the script engine so it can invoke Lua callbacks.
    std::vector<ContactEvent> DrainContactEvents();

    void* NativeUserData(u32 bodyId) const;

    bool IsInitialized() const { return m_Initialized; }

private:
    struct Impl;
    Scope<Impl> m_Impl;
    bool m_Initialized = false;
    vec3 m_Gravity{0.0f, -9.81f, 0.0f};
};

} // namespace fw
