#pragma once
// Central math header. Forgeworks uses GLM as its vector/matrix library and
// adds a handful of engine-specific helpers (transform decomposition, AABB,
// ray, plane) used by physics, rendering and the brush/CSG system alike.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <algorithm>

namespace fw {

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::ivec2;
using glm::ivec3;
using glm::quat;
using glm::mat3;
using glm::mat4;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1e-6f;

inline float Radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float Degrees(float radians) { return radians * (180.0f / kPi); }

// ---------------------------------------------------------------------------
// Transform: position / rotation (quaternion) / scale, with helpers to build
// and decompose a 4x4 matrix. This is the data the Transform component stores
// and what scripts manipulate via the scripting API.
// ---------------------------------------------------------------------------
struct Transform {
    vec3 position{0.0f, 0.0f, 0.0f};
    quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f, 1.0f, 1.0f};

    mat4 ToMatrix() const {
        mat4 t = glm::translate(mat4(1.0f), position);
        mat4 r = glm::toMat4(rotation);
        mat4 s = glm::scale(mat4(1.0f), scale);
        return t * r * s;
    }

    static Transform FromMatrix(const mat4& m) {
        Transform out;
        vec3 skew; vec4 persp;
        glm::decompose(m, out.scale, out.rotation, out.position, skew, persp);
        out.rotation = glm::conjugate(out.rotation);
        return out;
    }

    vec3 Forward() const { return rotation * vec3(0, 0, -1); }
    vec3 Right()   const { return rotation * vec3(1, 0, 0); }
    vec3 Up()      const { return rotation * vec3(0, 1, 0); }

    vec3 EulerDegrees() const { return glm::degrees(glm::eulerAngles(rotation)); }
    void SetEulerDegrees(const vec3& euler) { rotation = glm::quat(glm::radians(euler)); }
};

// ---------------------------------------------------------------------------
// Axis-aligned bounding box
// ---------------------------------------------------------------------------
struct AABB {
    vec3 min{ std::numeric_limits<float>::max() };
    vec3 max{ -std::numeric_limits<float>::max() };

    void Grow(const vec3& p) { min = glm::min(min, p); max = glm::max(max, p); }
    void Grow(const AABB& b) { min = glm::min(min, b.min); max = glm::max(max, b.max); }
    vec3 Center() const { return (min + max) * 0.5f; }
    vec3 Extents() const { return (max - min) * 0.5f; }
    bool Valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    AABB Transformed(const mat4& m) const {
        AABB result;
        vec3 corners[8] = {
            {min.x,min.y,min.z},{max.x,min.y,min.z},{min.x,max.y,min.z},{max.x,max.y,min.z},
            {min.x,min.y,max.z},{max.x,min.y,max.z},{min.x,max.y,max.z},{max.x,max.y,max.z}
        };
        for (auto& c : corners) result.Grow(vec3(m * vec4(c, 1.0f)));
        return result;
    }
};

// ---------------------------------------------------------------------------
// Ray / Plane, used by picking, raycasts and brush clipping
// ---------------------------------------------------------------------------
struct Ray {
    vec3 origin{0.0f};
    vec3 direction{0.0f, 0.0f, -1.0f};
    vec3 At(float t) const { return origin + direction * t; }
};

struct Plane {
    vec3 normal{0, 1, 0};
    float d = 0.0f; // normal . point + d = 0

    static Plane FromPointNormal(const vec3& point, const vec3& n) {
        vec3 nn = glm::normalize(n);
        return Plane{ nn, -glm::dot(nn, point) };
    }
    static Plane FromThreePoints(const vec3& a, const vec3& b, const vec3& c) {
        vec3 n = glm::normalize(glm::cross(b - a, c - a));
        return FromPointNormal(a, n);
    }
    float SignedDistance(const vec3& p) const { return glm::dot(normal, p) + d; }
};

inline bool RayPlaneIntersect(const Ray& ray, const Plane& plane, float& outT) {
    float denom = glm::dot(plane.normal, ray.direction);
    if (std::abs(denom) < kEpsilon) return false;
    outT = -(glm::dot(plane.normal, ray.origin) + plane.d) / denom;
    return outT >= 0.0f;
}

inline bool RayAABBIntersect(const Ray& ray, const AABB& box, float& outT) {
    vec3 invDir = 1.0f / ray.direction;
    vec3 t0 = (box.min - ray.origin) * invDir;
    vec3 t1 = (box.max - ray.origin) * invDir;
    vec3 tmin = glm::min(t0, t1);
    vec3 tmax = glm::max(t0, t1);
    float tNear = std::max(std::max(tmin.x, tmin.y), tmin.z);
    float tFar  = std::min(std::min(tmax.x, tmax.y), tmax.z);
    if (tNear > tFar || tFar < 0.0f) return false;
    outT = tNear >= 0.0f ? tNear : tFar;
    return true;
}

// Möller–Trumbore ray/triangle intersection, used for mesh picking in the editor.
inline bool RayTriangleIntersect(const Ray& ray, const vec3& v0, const vec3& v1, const vec3& v2, float& outT) {
    const float EPS = 1e-7f;
    vec3 e1 = v1 - v0, e2 = v2 - v0;
    vec3 h = glm::cross(ray.direction, e2);
    float a = glm::dot(e1, h);
    if (std::abs(a) < EPS) return false;
    float f = 1.0f / a;
    vec3 s = ray.origin - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    vec3 q = glm::cross(s, e1);
    float v = f * glm::dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    float t = f * glm::dot(e2, q);
    if (t <= EPS) return false;
    outT = t;
    return true;
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 Lerp(const vec3& a, const vec3& b, float t) { return a + (b - a) * t; }
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

} // namespace fw
