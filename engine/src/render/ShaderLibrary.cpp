#include "engine/render/ShaderLibrary.h"
#include "engine/render/RenderDevice.h"
#include "engine/core/Log.h"

#if FW_PLATFORM_WINDOWS
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace fw {

static ComPtr<ID3DBlob> CompileShaderFromFile(const std::string& path, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    std::wstring wpath(path.begin(), path.end());

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        FW_LOG_ERROR("Shader compile failed (%s:%s): %s", path.c_str(), entry,
            errors ? (const char*)errors->GetBufferPointer() : "unknown error");
        return nullptr;
    }
    return blob;
}

ShaderProgram* ShaderLibrary::LoadMeshShader(const std::string& path) {
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second.get();

    auto vsBlob = CompileShaderFromFile(path, "VSMain", "vs_5_0");
    auto psBlob = CompileShaderFromFile(path, "PSMain", "ps_5_0");
    if (!vsBlob || !psBlob) return nullptr;

    auto program = MakeScope<ShaderProgram>();
    m_Device->Device()->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &program->vs);
    m_Device->Device()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &program->ps);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_Device->Device()->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &program->inputLayout);

    ShaderProgram* raw = program.get();
    m_Cache[path] = std::move(program);
    FW_LOG_INFO("Compiled mesh shader: %s", path.c_str());
    return raw;
}

ShaderProgram* ShaderLibrary::LoadDepthOnlyShader(const std::string& path) {
    std::string key = path + "#depth";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();

    auto vsBlob = CompileShaderFromFile(path, "VSMain", "vs_5_0");
    if (!vsBlob) return nullptr;

    auto program = MakeScope<ShaderProgram>();
    m_Device->Device()->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &program->vs);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_Device->Device()->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &program->inputLayout);

    ShaderProgram* raw = program.get();
    m_Cache[key] = std::move(program);
    return raw;
}

ShaderProgram* ShaderLibrary::LoadFullscreenShader(const std::string& path) {
    std::string key = path + "#fullscreen";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();

    auto vsBlob = CompileShaderFromFile(path, "VSMain", "vs_5_0");
    auto psBlob = CompileShaderFromFile(path, "PSMain", "ps_5_0");
    if (!vsBlob || !psBlob) return nullptr;

    auto program = MakeScope<ShaderProgram>();
    m_Device->Device()->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &program->vs);
    m_Device->Device()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &program->ps);
    // No input layout: fullscreen triangle is generated purely from SV_VertexID.

    ShaderProgram* raw = program.get();
    m_Cache[key] = std::move(program);
    return raw;
}

void ShaderLibrary::Clear() { m_Cache.clear(); }

} // namespace fw

#endif
