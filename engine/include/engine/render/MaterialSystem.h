#pragma once
// Materials are simple JSON (.fwmat) files describing a PBR-ish parameter
// set + optional texture maps. MaterialSystem loads/caches them and uploads
// per-material constant buffer data + binds textures before a draw call.

#include "engine/core/Base.h"
#include "engine/math/Math.h"
#include <string>
#include <unordered_map>

#if FW_PLATFORM_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace fw {

class RenderDevice;
class TextureLoader;
struct GpuTexture;

struct Material {
    vec4 albedoColor{1.0f};
    vec3 emissiveColor{0.0f};
    float emissiveStrength = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.8f;
    std::string albedoMap;
    std::string normalMap;
    std::string metallicRoughnessMap;
    std::string emissiveMap;
    bool doubleSided = false;
    bool transparent = false;
    std::string shaderAsset = "assets/shaders/Mesh.hlsl";
};

class MaterialSystem {
public:
    MaterialSystem(RenderDevice* device, TextureLoader* textures) : m_Device(device), m_Textures(textures) {}

    Material* Load(const std::string& path);
    Material* GetDefault();
    bool Save(const std::string& path, const Material& mat);

#if FW_PLATFORM_WINDOWS
    // Binds this material's textures (t0-t3) and updates the material
    // constant buffer (b3) on the given context.
    void Bind(ID3D11DeviceContext* ctx, ID3D11Buffer* materialCB, const Material& mat);
#endif

private:
    RenderDevice* m_Device;
    TextureLoader* m_Textures;
    std::unordered_map<std::string, Scope<Material>> m_Cache;
};

} // namespace fw
