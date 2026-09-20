#pragma once
// All built-in ECS components. Components are plain data (EnTT convention);
// behaviour lives in systems (engine/*) or in Lua via ScriptComponent.

#include "engine/core/Base.h"
#include "engine/core/UUID.h"
#include "engine/math/Math.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace fw {

// Every entity has an IDComponent (stable UUID) and a NameComponent.
struct IDComponent {
    UUID id;
};

struct NameComponent {
    std::string name = "Entity";
};

// Scene-graph parenting. Children store their parent; Transform is always
// local-space relative to the parent (or world-space if no parent).
struct HierarchyComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children;
};

struct TransformComponent {
    Transform local;
    mat4 worldMatrix{1.0f}; // recomputed each frame by the transform system
};

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
enum class LightType : u8 { Directional, Point, Spot };

struct LightComponent {
    LightType type = LightType::Point;
    vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDeg = 25.0f;
    float outerConeDeg = 35.0f;
    bool castsShadows = true;
    bool isStatic = false; // if true, contributes to baked lightmaps instead of (or in addition to) real-time
};

struct MeshRendererComponent {
    std::string meshAsset;       // path to .fwmesh / .obj under assets/
    std::vector<std::string> materialSlots; // path to .fwmat per submesh
    bool castShadows = true;
    bool receiveShadows = true;
    bool useLightmap = false;
    std::string lightmapAsset;   // baked lightmap texture path, set by the lightmap baker
    int lightmapChannel = 1;     // UV channel used for lightmap coordinates
};

struct CameraComponent {
    bool isPrimary = true;
    float fovDeg = 60.0f;
    float nearClip = 0.05f;
    float farClip = 2000.0f;
    bool orthographic = false;
    float orthoSize = 10.0f;
};

struct SkyLightComponent {
    vec3 ambientColor{0.15f, 0.17f, 0.2f};
    float ambientIntensity = 1.0f;
    std::string skyboxAsset; // cubemap or procedural sky descriptor
};

// ---------------------------------------------------------------------------
// Physics (mirrors what's created in Jolt; PhysicsWorld owns the actual body)
// ---------------------------------------------------------------------------
enum class ColliderShape : u8 { Box, Sphere, Capsule, ConvexHull, TriangleMesh };
enum class BodyMotionType : u8 { Static, Kinematic, Dynamic };

struct RigidBodyComponent {
    BodyMotionType motionType = BodyMotionType::Static;
    float mass = 1.0f;
    float friction = 0.6f;
    float restitution = 0.1f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool isTrigger = false;
    bool gravityEnabled = true;
    // runtime handle into the physics world (opaque, Jolt BodyID packed as u32)
    u32 runtimeBodyId = 0xFFFFFFFF;
};

struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    vec3 halfExtents{0.5f, 0.5f, 0.5f}; // box
    float radius = 0.5f;                // sphere/capsule
    float height = 1.0f;                // capsule
    vec3 offset{0.0f};
    std::string collisionMeshAsset;      // for TriangleMesh/ConvexHull, source mesh
};

struct CharacterControllerComponent {
    float radius = 0.4f;
    float height = 1.8f;
    float maxSlopeDeg = 50.0f;
    float stepHeight = 0.3f;
    vec3 velocity{0.0f};
    bool isGrounded = false;
    u32 runtimeCharacterId = 0xFFFFFFFF;
};

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
struct AudioSourceComponent {
    std::string soundAsset;
    bool is3D = true;
    bool loop = false;
    bool playOnStart = false;
    float volume = 1.0f;
    float pitch = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    u32 runtimeVoiceId = 0xFFFFFFFF;
};

struct AudioListenerComponent {
    bool isPrimary = true;
};

// ---------------------------------------------------------------------------
// Scripting
// ---------------------------------------------------------------------------
struct ScriptComponent {
    std::string scriptAsset; // path to a .lua file
    // Arbitrary key/value properties exposed in the inspector and passed into
    // the script's `Properties` table on init (like Godot's exported vars).
    std::unordered_map<std::string, std::string> properties;
    bool enabled = true;

    // Runtime-only (not serialized): opaque handle managed by ScriptEngine.
    void* runtimeInstance = nullptr;
};

// ---------------------------------------------------------------------------
// Brush / Hammer-mode geometry (see engine/brush/Brush.h for the CSG math)
// ---------------------------------------------------------------------------
struct BrushComponent {
    UUID brushId; // index into the level's brush list (Brush geometry itself lives in BrushMap)
    bool isTrigger = false;
    std::string classname = "brush"; // Hammer-style classname, "" for plain geometry brushes
};

} // namespace fw
