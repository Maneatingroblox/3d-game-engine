#pragma once
// Compiles HLSL shaders (via D3DCompile at runtime, so no offline shader
// build step is required) into vertex/pixel shaders + an input layout, and
// caches them by source path.

#include "engine/core/Base.h"
#include <string>
#include <unordered_map>

#if FW_PLATFORM_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace fw {

class RenderDevice;

struct ShaderProgram {
#if FW_PLATFORM_WINDOWS
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> inputLayout;
#endif
};

class ShaderLibrary {
public:
    explicit ShaderLibrary(RenderDevice* device) : m_Device(device) {}

    // Compiles VSMain/PSMain from `path`, building an input layout from the
    // standard fw::Vertex format (position/normal/tangent/uv0/uv1/color).
    ShaderProgram* LoadMeshShader(const std::string& path);
    // Depth-only shader with a position-only input layout (shadow pass).
    ShaderProgram* LoadDepthOnlyShader(const std::string& path);
    // Fullscreen-triangle shader with no vertex input (skybox/post-process).
    ShaderProgram* LoadFullscreenShader(const std::string& path);

    void Clear();

private:
    RenderDevice* m_Device;
    std::unordered_map<std::string, Scope<ShaderProgram>> m_Cache;
};

} // namespace fw
