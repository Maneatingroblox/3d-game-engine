#include "engine/physics/PhysicsSystem.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/scene/Scene.h"
#include "engine/script/ScriptEngine.h"
#include "engine/core/Log.h"

namespace fw {

PhysicsSystem::PhysicsSystem(Scene* scene, PhysicsWorld* world) : m_Scene(scene), m_World(world) {}

void PhysicsSystem::SyncNewBodies() {
    // Rigid bodies
    m_Scene->Each<TransformComponent, RigidBodyComponent, ColliderComponent>(
        [this](Entity e, TransformComponent& tc, RigidBodyComponent& rb, ColliderComponent& col) {
            if (rb.runtimeBodyId != 0xFFFFFFFF) return;

            BodyCreateInfo info;
            info.position = tc.local.position;
            info.rotation = tc.local.rotation;
            info.mass = rb.mass;
            info.friction = rb.friction;
            info.restitution = rb.restitution;
            info.linearDamping = rb.linearDamping;
            info.angularDamping = rb.angularDamping;
            info.isTrigger = rb.isTrigger;
            info.isStatic = (rb.motionType == BodyMotionType::Static);
            info.isKinematic = (rb.motionType == BodyMotionType::Kinematic);
            info.gravityEnabled = rb.gravityEnabled;
            info.halfExtents = col.halfExtents * tc.local.scale;
            info.radius = col.radius * std::max({tc.local.scale.x, tc.local.scale.y, tc.local.scale.z});
            info.height = col.height * tc.local.scale.y;

            switch (col.shape) {
                case ColliderShape::Box: info.shape = BodyCreateInfo::Shape::Box; break;
                case ColliderShape::Sphere: info.shape = BodyCreateInfo::Shape::Sphere; break;
                case ColliderShape::Capsule: info.shape = BodyCreateInfo::Shape::Capsule; break;
                case ColliderShape::ConvexHull: info.shape = BodyCreateInfo::Shape::ConvexHull; break;
                case ColliderShape::TriangleMesh: info.shape = BodyCreateInfo::Shape::TriangleMesh; break;
            }

            u32 id = m_World->CreateBody(info);
            rb.runtimeBodyId = id;
            if (id != 0xFFFFFFFF) m_BodyToEntity[id] = e.Handle();
        });

    // Character controllers
    m_Scene->Each<TransformComponent, CharacterControllerComponent>(
        [this](Entity e, TransformComponent& tc, CharacterControllerComponent& cc) {
            if (cc.runtimeCharacterId != 0xFFFFFFFF) return;
            u32 id = m_World->CreateCharacter(tc.local.position, cc.radius, cc.height, cc.maxSlopeDeg);
            cc.runtimeCharacterId = id;
            if (id != 0xFFFFFFFF) m_CharacterToEntity[id] = e.Handle();
        });
}

void PhysicsSystem::RemoveOrphanBodies() {
    std::vector<u32> toRemove;
    for (auto& [bodyId, ent] : m_BodyToEntity) {
        if (!m_Scene->Registry().valid(ent) || !m_Scene->Registry().all_of<RigidBodyComponent>(ent)) {
            m_World->DestroyBody(bodyId);
            toRemove.push_back(bodyId);
        }
    }
    for (auto id : toRemove) m_BodyToEntity.erase(id);
}

entt::entity PhysicsSystem::EntityForBody(u32 bodyId) const {
    auto it = m_BodyToEntity.find(bodyId);
    return it != m_BodyToEntity.end() ? it->second : entt::null;
}

void PhysicsSystem::Step(float dt, ScriptEngine* scriptEngine) {
    SyncNewBodies();

    // Push kinematic body transforms from ECS -> physics before stepping.
    m_Scene->Each<TransformComponent, RigidBodyComponent>([this](Entity, TransformComponent& tc, RigidBodyComponent& rb) {
        if (rb.motionType == BodyMotionType::Kinematic && rb.runtimeBodyId != 0xFFFFFFFF) {
            m_World->SetBodyTransform(rb.runtimeBodyId, tc.local.position, tc.local.rotation);
        }
    });

    m_World->Step(dt);

    // Pull dynamic body transforms back into ECS.
    m_Scene->Each<TransformComponent, RigidBodyComponent>([this](Entity, TransformComponent& tc, RigidBodyComponent& rb) {
        if (rb.motionType == BodyMotionType::Dynamic && rb.runtimeBodyId != 0xFFFFFFFF) {
            vec3 pos; quat rot;
            m_World->GetBodyTransform(rb.runtimeBodyId, pos, rot);
            tc.local.position = pos;
            tc.local.rotation = rot;
        }
    });

    // Character controllers: apply accumulated velocity as a displacement.
    m_Scene->Each<TransformComponent, CharacterControllerComponent>([this, dt](Entity, TransformComponent& tc, CharacterControllerComponent& cc) {
        if (cc.runtimeCharacterId == 0xFFFFFFFF) return;
        vec3 displacement = cc.velocity * dt;
        m_World->MoveCharacter(cc.runtimeCharacterId, displacement, dt);
        cc.isGrounded = m_World->IsCharacterGrounded(cc.runtimeCharacterId);
        tc.local.position = m_World->CharacterPosition(cc.runtimeCharacterId);
    });

    if (scriptEngine) {
        auto events = m_World->DrainContactEvents();
        for (auto& ev : events) {
            entt::entity ea = EntityForBody(ev.bodyA);
            entt::entity eb = EntityForBody(ev.bodyB);
            if (ea == entt::null || eb == entt::null) continue;
            scriptEngine->DispatchCollision(ea, eb, ev.isTrigger, ev.isStart);
            scriptEngine->DispatchCollision(eb, ea, ev.isTrigger, ev.isStart);
        }
    }
}

void PhysicsSystem::Clear() {
    for (auto& [id, ent] : m_BodyToEntity) m_World->DestroyBody(id);
    for (auto& [id, ent] : m_CharacterToEntity) m_World->DestroyCharacter(id);
    m_BodyToEntity.clear();
    m_CharacterToEntity.clear();
}

} // namespace fw
