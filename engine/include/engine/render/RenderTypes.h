#pragma once
// Renderer-facing value types + camera math shared by every renderer backend.
//
// IMPORTANT CONVENTION (this is the single source of truth for the whole
// rendering stack, HLSL included):
//
//   * World/view space is OpenGL-style right handed (+X right, +Y up,
//     -Z forward), which is what glm::lookAt()/Transform::Forward() produce.
//   * Matrices are glm matrices (column-major storage) and are uploaded to
//     HLSL constant buffers verbatim. HLSL's default matrix packing is
//     column-major too, so a shader's float4x4 holds exactly the glm matrix
//     and every transform in HLSL must be written as mul(matrix, vector) -
//     NOT mul(vector, matrix), which would use the transposed matrix and
//     (for a perspective matrix) push every vertex outside the clip volume.
//   * Clip space depth is Direct3D-style [0,1] via glm::perspectiveRH_ZO so
//     the whole depth buffer range is usable.
//
// engine/src/render/SoftwareRenderer.cpp reproduces this same math on the CPU
// and engine_tests verifies it, so the CPU and GPU paths cannot silently
// drift apart again.

#include "engine/core/Base.h"
#include "engine/math/Math.h"

namespace fw {

struct RenderCamera {
    vec3 position{0.0f};
    mat4 view{1.0f};
    mat4 proj{1.0f};

    mat4 ViewProj() const { return proj * view; }

    // World-space direction the camera looks along. The third column of the
    // inverse view matrix is the camera's local +Z axis, which points *behind*
    // a right-handed camera (it looks down -Z), hence the negation.
    vec3 Forward() const {
        const mat4 inv = glm::inverse(view);
        return -glm::normalize(vec3(inv[2]));
    }
};

struct RenderSettings {
    vec3 ambientColor{0.15f, 0.17f, 0.2f};
    float ambientIntensity = 1.0f;
    bool enableShadows = true;
    int shadowMapResolution = 2048;
    bool wireframe = false;
    bool drawGrid = true;
    bool drawColliders = false;
    // World-space size of one grid cell (the editor grid snaps/logs at this
    // spacing); 0 disables the grid entirely.
    float gridCellSize = 1.0f;
    // Background used when a scene has no sky/skybox (also the software
    // renderer's clear colour).
    vec3 clearColor{0.05f, 0.05f, 0.06f};
};

// ---- camera helpers -------------------------------------------------------

// Direct3D-style perspective projection (depth range [0,1], right handed).
inline mat4 MakeProjectionMatrix(float fovDeg, float aspect, float nearClip, float farClip) {
    if (aspect <= 0.0f) aspect = 16.0f / 9.0f;
    if (nearClip <= 0.0f) nearClip = 0.05f;
    if (farClip <= nearClip) farClip = nearClip + 1.0f;
    return glm::perspectiveRH_ZO(Radians(fovDeg), aspect, nearClip, farClip);
}

// View direction for a yaw/pitch camera. This is deliberately the *same*
// convention as Transform::SetEulerDegrees(vec3(pitch, yaw, 0)) (i.e. glm's
// R_y(yaw) * R_x(pitch), a positive rotation about +Y), so an entity's
// CameraComponent and the editor's fly camera can never disagree:
//
//   yaw   = 0     -> looking down -Z
//   yaw   > 0     -> turning towards -X (counter-clockwise seen from above)
//   pitch > 0     -> looking up
inline vec3 YawPitchForward(float yawDeg, float pitchDeg) {
    const float yaw = Radians(yawDeg);
    const float pitch = Radians(pitchDeg);
    return glm::normalize(vec3(
        -std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch)));
}

inline mat4 MakeViewMatrix(const vec3& position, float yawDeg, float pitchDeg) {
    vec3 forward = YawPitchForward(yawDeg, pitchDeg);
    return glm::lookAt(position, position + forward, vec3(0, 1, 0));
}

// A consistently-built camera for a position + orientation.
inline RenderCamera MakeCamera(const vec3& position, float yawDeg, float pitchDeg,
                               float fovDeg, float aspect, float nearClip, float farClip) {
    RenderCamera cam;
    cam.position = position;
    cam.view = MakeViewMatrix(position, yawDeg, pitchDeg);
    cam.proj = MakeProjectionMatrix(fovDeg, aspect, nearClip, farClip);
    return cam;
}

} // namespace fw
