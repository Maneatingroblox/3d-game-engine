#include "engine/physics/PhysicsWorld.h"
#include "engine/core/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <thread>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

namespace fw {

// ---------------------------------------------------------------------------
// Object / broadphase layers
// ---------------------------------------------------------------------------
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr ObjectLayer TRIGGER = 2;
    static constexpr ObjectLayer NUM_LAYERS = 3;
}

namespace BPLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS = 2;
}

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        // Everything collides with everything for simplicity; triggers are
        // filtered at the contact-listener level (report but don't respond).
        FW_UNUSED(inObject1); FW_UNUSED(inObject2);
        return true;
    }
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BPLayers::MOVING;
        mObjectToBroadPhase[Layers::TRIGGER] = BPLayers::MOVING;
    }
    uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override { return mObjectToBroadPhase[inLayer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "Layer"; }
#endif
private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer, BroadPhaseLayer) const override { return true; }
};

class ContactListenerImpl final : public ContactListener {
public:
    std::vector<ContactEvent>* events = nullptr;
    std::unordered_map<BodyID, bool>* triggerBodies = nullptr;

    ValidateResult OnContactValidate(const Body&, const Body&, RVec3Arg, const CollideShapeResult&) override {
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }
    void OnContactAdded(const Body& inBody1, const Body& inBody2, const ContactManifold&, ContactSettings&) override {
        bool trig = (triggerBodies && (triggerBodies->count(inBody1.GetID()) || triggerBodies->count(inBody2.GetID())));
        if (events) events->push_back({ inBody1.GetID().GetIndexAndSequenceNumber(), inBody2.GetID().GetIndexAndSequenceNumber(), trig, true });
    }
    void OnContactRemoved(const SubShapeIDPair& pair) override {
        bool trig = (triggerBodies && (triggerBodies->count(pair.GetBody1ID()) || triggerBodies->count(pair.GetBody2ID())));
        if (events) events->push_back({ pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetBody2ID().GetIndexAndSequenceNumber(), trig, false });
    }
};

static void TraceImpl(const char* fmt, ...) {
    char buf[1024];
    va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    FW_LOG_TRACE("[Jolt] %s", buf);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* expr, const char* msg, const char* file, JPH::uint line) {
    FW_LOG_ERROR("[Jolt Assert] %s:%u (%s) %s", file, line, expr, msg ? msg : "");
    return true;
}
#endif

static vec3 J2G(RVec3Arg v) { return vec3((float)v.GetX(), (float)v.GetY(), (float)v.GetZ()); }
static Vec3 G2J(const vec3& v) { return Vec3(v.x, v.y, v.z); }
static quat J2G(QuatArg q) { return quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
static Quat G2J(const quat& q) { return Quat(q.x, q.y, q.z, q.w); }

struct PhysicsWorld::Impl {
    Scope<TempAllocatorImpl> tempAllocator;
    Scope<JobSystemThreadPool> jobSystem;
    Scope<PhysicsSystem> physicsSystem;
    Scope<BPLayerInterfaceImpl> bpLayerInterface;
    Scope<ObjectVsBroadPhaseLayerFilterImpl> objVsBpFilter;
    Scope<ObjectLayerPairFilterImpl> objVsObjFilter;
    Scope<ContactListenerImpl> contactListener;

    std::vector<ContactEvent> pendingEvents;
    std::unordered_map<BodyID, bool> triggerBodies;
    std::unordered_map<u32, void*> userData;

    struct CharacterEntry {
        JPH::Ref<CharacterVirtual> character;
        Scope<CharacterVirtual::ExtendedUpdateSettings> updateSettings;
    };
    std::unordered_map<u32, CharacterEntry> characters;
    u32 nextCharacterId = 1;

    float accumulator = 0.0f;
};

PhysicsWorld::PhysicsWorld() : m_Impl(MakeScope<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { Shutdown(); }

static bool s_JoltTypesRegistered = false;

void PhysicsWorld::Init() {
    if (!s_JoltTypesRegistered) {
        RegisterDefaultAllocator();
        Trace = TraceImpl;
        JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
        Factory::sInstance = new Factory();
        RegisterTypes();
        s_JoltTypesRegistered = true;
    }

    m_Impl->tempAllocator = MakeScope<TempAllocatorImpl>(64 * 1024 * 1024);
    unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    m_Impl->jobSystem = MakeScope<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, (int)numThreads);

    const uint cMaxBodies = 65536;
    const uint cNumBodyMutexes = 0;
    const uint cMaxBodyPairs = 65536;
    const uint cMaxContactConstraints = 20480;

    m_Impl->bpLayerInterface = MakeScope<BPLayerInterfaceImpl>();
    m_Impl->objVsBpFilter = MakeScope<ObjectVsBroadPhaseLayerFilterImpl>();
    m_Impl->objVsObjFilter = MakeScope<ObjectLayerPairFilterImpl>();

    m_Impl->physicsSystem = MakeScope<PhysicsSystem>();
    m_Impl->physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        *m_Impl->bpLayerInterface, *m_Impl->objVsBpFilter, *m_Impl->objVsObjFilter);

    m_Impl->contactListener = MakeScope<ContactListenerImpl>();
    m_Impl->contactListener->events = &m_Impl->pendingEvents;
    m_Impl->contactListener->triggerBodies = &m_Impl->triggerBodies;
    m_Impl->physicsSystem->SetContactListener(m_Impl->contactListener.get());
    m_Impl->physicsSystem->SetGravity(G2J(m_Gravity));

    m_Initialized = true;
    FW_LOG_INFO("PhysicsWorld initialized (Jolt Physics)");
}

void PhysicsWorld::Shutdown() {
    if (!m_Initialized) return;
    m_Impl->characters.clear();
    m_Impl->physicsSystem.reset();
    m_Impl->jobSystem.reset();
    m_Impl->tempAllocator.reset();
    m_Initialized = false;
}

void PhysicsWorld::Step(float dt) {
    if (!m_Initialized) return;
    const float fixedStep = 1.0f / 60.0f;
    m_Impl->accumulator += dt;
    int steps = 0;
    while (m_Impl->accumulator >= fixedStep && steps < 8) {
        m_Impl->physicsSystem->Update(fixedStep, 1, m_Impl->tempAllocator.get(), m_Impl->jobSystem.get());
        m_Impl->accumulator -= fixedStep;
        steps++;
    }
}

static RefConst<Shape> MakeShape(const BodyCreateInfo& info) {
    switch (info.shape) {
        case BodyCreateInfo::Shape::Sphere:
            return new SphereShape(info.radius);
        case BodyCreateInfo::Shape::Capsule: {
            float halfHeight = std::max(0.01f, info.height * 0.5f - info.radius);
            return new CapsuleShape(halfHeight, info.radius);
        }
        case BodyCreateInfo::Shape::ConvexHull: {
            if (!info.meshVertices || info.meshVertices->empty()) return new BoxShape(G2J(info.halfExtents));
            Array<Vec3> points;
            points.reserve(info.meshVertices->size());
            for (auto& v : *info.meshVertices) points.push_back(G2J(v));
            ConvexHullShapeSettings settings(points);
            auto result = settings.Create();
            if (result.IsValid()) return result.Get();
            return new BoxShape(G2J(info.halfExtents));
        }
        case BodyCreateInfo::Shape::TriangleMesh: {
            if (!info.meshVertices || !info.meshIndices || info.meshIndices->size() < 3) return new BoxShape(G2J(info.halfExtents));
            VertexList verts;
            verts.reserve(info.meshVertices->size());
            for (auto& v : *info.meshVertices) verts.push_back(Float3(v.x, v.y, v.z));
            IndexedTriangleList tris;
            tris.reserve(info.meshIndices->size() / 3);
            for (size_t i = 0; i + 2 < info.meshIndices->size(); i += 3)
                tris.push_back(IndexedTriangle((*info.meshIndices)[i], (*info.meshIndices)[i+1], (*info.meshIndices)[i+2]));
            MeshShapeSettings settings(verts, tris);
            auto result = settings.Create();
            if (result.IsValid()) return result.Get();
            return new BoxShape(G2J(info.halfExtents));
        }
        case BodyCreateInfo::Shape::Box:
        default:
            return new BoxShape(G2J(info.halfExtents));
    }
}

u32 PhysicsWorld::CreateBody(const BodyCreateInfo& info) {
    if (!m_Initialized) return 0xFFFFFFFF;
    BodyInterface& bi = m_Impl->physicsSystem->GetBodyInterface();

    RefConst<Shape> shape = MakeShape(info);

    EMotionType motionType = EMotionType::Static;
    ObjectLayer layer = Layers::NON_MOVING;
    if (info.isTrigger) { motionType = info.isKinematic ? EMotionType::Kinematic : EMotionType::Dynamic; layer = Layers::TRIGGER; }
    else if (info.isKinematic) { motionType = EMotionType::Kinematic; layer = Layers::MOVING; }
    else if (!info.isStatic) { motionType = EMotionType::Dynamic; layer = Layers::MOVING; }

    BodyCreationSettings settings(shape, RVec3(info.position.x, info.position.y, info.position.z),
        G2J(info.rotation), motionType, layer);
    settings.mFriction = info.friction;
    settings.mRestitution = info.restitution;
    settings.mLinearDamping = info.linearDamping;
    settings.mAngularDamping = info.angularDamping;
    settings.mIsSensor = info.isTrigger;
    settings.mGravityFactor = info.gravityEnabled ? 1.0f : 0.0f;
    if (motionType == EMotionType::Dynamic) {
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = std::max(0.001f, info.mass);
    }

    Body* body = bi.CreateBody(settings);
    if (!body) return 0xFFFFFFFF;
    bi.AddBody(body->GetID(), motionType == EMotionType::Static ? EActivation::DontActivate : EActivation::Activate);

    u32 idPacked = body->GetID().GetIndexAndSequenceNumber();
    if (info.isTrigger) m_Impl->triggerBodies[body->GetID()] = true;
    if (info.userData) m_Impl->userData[idPacked] = info.userData;
    return idPacked;
}

void PhysicsWorld::DestroyBody(u32 bodyId) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    BodyID id(bodyId);
    BodyInterface& bi = m_Impl->physicsSystem->GetBodyInterface();
    bi.RemoveBody(id);
    bi.DestroyBody(id);
    m_Impl->triggerBodies.erase(id);
    m_Impl->userData.erase(bodyId);
}

void PhysicsWorld::SetBodyTransform(u32 bodyId, const vec3& position, const quat& rotation) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    m_Impl->physicsSystem->GetBodyInterface().SetPositionAndRotation(
        BodyID(bodyId), RVec3(position.x, position.y, position.z), G2J(rotation), EActivation::Activate);
}

void PhysicsWorld::GetBodyTransform(u32 bodyId, vec3& outPosition, quat& outRotation) const {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    RVec3 pos; Quat rot;
    m_Impl->physicsSystem->GetBodyInterface().GetPositionAndRotation(BodyID(bodyId), pos, rot);
    outPosition = J2G(pos);
    outRotation = J2G(rot);
}

void PhysicsWorld::SetBodyVelocity(u32 bodyId, const vec3& linear, const vec3& angular) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    m_Impl->physicsSystem->GetBodyInterface().SetLinearAndAngularVelocity(BodyID(bodyId), G2J(linear), G2J(angular));
}

void PhysicsWorld::GetBodyVelocity(u32 bodyId, vec3& outLinear, vec3& outAngular) const {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    Vec3 lin, ang;
    m_Impl->physicsSystem->GetBodyInterface().GetLinearAndAngularVelocity(BodyID(bodyId), lin, ang);
    outLinear = vec3(lin.GetX(), lin.GetY(), lin.GetZ());
    outAngular = vec3(ang.GetX(), ang.GetY(), ang.GetZ());
}

void PhysicsWorld::AddForce(u32 bodyId, const vec3& force) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    m_Impl->physicsSystem->GetBodyInterface().AddForce(BodyID(bodyId), G2J(force));
}

void PhysicsWorld::AddImpulse(u32 bodyId, const vec3& impulse) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    m_Impl->physicsSystem->GetBodyInterface().AddImpulse(BodyID(bodyId), G2J(impulse));
}

void PhysicsWorld::SetGravityEnabled(u32 bodyId, bool enabled) {
    if (!m_Initialized || bodyId == 0xFFFFFFFF) return;
    m_Impl->physicsSystem->GetBodyInterface().SetGravityFactor(BodyID(bodyId), enabled ? 1.0f : 0.0f);
}

RaycastHit PhysicsWorld::Raycast(const vec3& origin, const vec3& direction, float maxDistance) {
    RaycastHit hit;
    if (!m_Initialized) return hit;
    RRayCast ray{ RVec3(origin.x, origin.y, origin.z), G2J(glm::normalize(direction)) * maxDistance };
    RayCastResult result;
    bool found = m_Impl->physicsSystem->GetNarrowPhaseQuery().CastRay(ray, result);
    if (found) {
        hit.hit = true;
        hit.distance = result.mFraction * maxDistance;
        hit.point = origin + glm::normalize(direction) * hit.distance;
        hit.bodyId = result.mBodyID.GetIndexAndSequenceNumber();
        BodyLockRead lock(m_Impl->physicsSystem->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            hit.normal = J2G(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, RVec3(hit.point.x, hit.point.y, hit.point.z)));
        }
    }
    return hit;
}

std::vector<RaycastHit> PhysicsWorld::RaycastAll(const vec3& origin, const vec3& direction, float maxDistance) {
    std::vector<RaycastHit> hits;
    if (!m_Initialized) return hits;
    RRayCast ray{ RVec3(origin.x, origin.y, origin.z), G2J(glm::normalize(direction)) * maxDistance };
    AllHitCollisionCollector<CastRayCollector> collector;
    m_Impl->physicsSystem->GetNarrowPhaseQuery().CastRay(ray, {}, collector);
    for (auto& r : collector.mHits) {
        RaycastHit h;
        h.hit = true;
        h.distance = r.mFraction * maxDistance;
        h.point = origin + glm::normalize(direction) * h.distance;
        h.bodyId = r.mBodyID.GetIndexAndSequenceNumber();
        hits.push_back(h);
    }
    return hits;
}

bool PhysicsWorld::OverlapSphere(const vec3& center, float radius, std::vector<u32>& outBodies) {
    if (!m_Initialized) return false;
    SphereShape sphere(radius);
    Mat44 com = Mat44::sTranslation(G2J(center));
    AllHitCollisionCollector<CollideShapeCollector> collector;
    CollideShapeSettings settings;
    m_Impl->physicsSystem->GetNarrowPhaseQuery().CollideShape(&sphere, Vec3::sReplicate(1.0f), com, settings, RVec3(center.x, center.y, center.z), collector);
    for (auto& r : collector.mHits) outBodies.push_back(r.mBodyID2.GetIndexAndSequenceNumber());
    return !outBodies.empty();
}

void PhysicsWorld::SetGravity(const vec3& g) {
    m_Gravity = g;
    if (m_Initialized) m_Impl->physicsSystem->SetGravity(G2J(g));
}

u32 PhysicsWorld::CreateCharacter(const vec3& position, float radius, float height, float maxSlopeDeg) {
    if (!m_Initialized) return 0xFFFFFFFF;
    float halfHeight = std::max(0.01f, height * 0.5f - radius);
    RefConst<Shape> shape = new CapsuleShape(halfHeight, radius);

    CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mMaxSlopeAngle = Radians(maxSlopeDeg);
    settings.mMass = 80.0f;
    settings.mSupportingVolume = JPH::Plane(Vec3::sAxisY(), -radius);

    Impl::CharacterEntry entry;
    entry.character = new CharacterVirtual(&settings, RVec3(position.x, position.y, position.z), Quat::sIdentity(), 0, m_Impl->physicsSystem.get());
    entry.updateSettings = MakeScope<CharacterVirtual::ExtendedUpdateSettings>();

    u32 id = m_Impl->nextCharacterId++;
    m_Impl->characters[id] = std::move(entry);
    return id;
}

void PhysicsWorld::DestroyCharacter(u32 characterId) {
    m_Impl->characters.erase(characterId);
}

vec3 PhysicsWorld::MoveCharacter(u32 characterId, const vec3& displacement, float dt) {
    auto it = m_Impl->characters.find(characterId);
    if (it == m_Impl->characters.end()) return vec3(0.0f);
    auto& ch = it->second.character;

    ObjectLayerPairFilterImpl objFilter;
    ObjectVsBroadPhaseLayerFilterImpl bpFilter;
    DefaultBroadPhaseLayerFilter bpLayerFilter(*m_Impl->objVsBpFilter, Layers::MOVING);
    DefaultObjectLayerFilter objLayerFilter(*m_Impl->objVsObjFilter, Layers::MOVING);
    BodyFilter bodyFilter;
    ShapeFilter shapeFilter;

    ch->ExtendedUpdate(dt, ch->GetUp() * m_Impl->physicsSystem->GetGravity().Length() * -1.0f + G2J(displacement) / std::max(dt, 1e-4f),
        *it->second.updateSettings, bpLayerFilter, objLayerFilter, bodyFilter, shapeFilter, *m_Impl->tempAllocator);

    return J2G(ch->GetPosition());
}

bool PhysicsWorld::IsCharacterGrounded(u32 characterId) const {
    auto it = m_Impl->characters.find(characterId);
    if (it == m_Impl->characters.end()) return false;
    return it->second.character->GetGroundState() == CharacterBase::EGroundState::OnGround;
}

vec3 PhysicsWorld::CharacterPosition(u32 characterId) const {
    auto it = m_Impl->characters.find(characterId);
    if (it == m_Impl->characters.end()) return vec3(0.0f);
    return J2G(it->second.character->GetPosition());
}

void PhysicsWorld::SetCharacterPosition(u32 characterId, const vec3& pos) {
    auto it = m_Impl->characters.find(characterId);
    if (it == m_Impl->characters.end()) return;
    it->second.character->SetPosition(RVec3(pos.x, pos.y, pos.z));
}

std::vector<ContactEvent> PhysicsWorld::DrainContactEvents() {
    std::vector<ContactEvent> out;
    std::swap(out, m_Impl->pendingEvents);
    return out;
}

void* PhysicsWorld::NativeUserData(u32 bodyId) const {
    auto it = m_Impl->userData.find(bodyId);
    return it != m_Impl->userData.end() ? it->second : nullptr;
}

} // namespace fw
