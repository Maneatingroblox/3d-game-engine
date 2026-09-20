#include "engine/render/MaterialSystem.h"
#include "engine/asset/TextureLoader.h"
#include "engine/render/RenderDevice.h"
#include "engine/core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace fw {

Material* MaterialSystem::GetDefault() {
    const std::string key = "__default__";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();
    auto mat = MakeScope<Material>();
    Material* raw = mat.get();
    m_Cache[key] = std::move(mat);
    return raw;
}

Material* MaterialSystem::Load(const std::string& path) {
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second.get();

    std::ifstream f(path);
    if (!f) {
        FW_LOG_WARN("Material not found, using default: %s", path.c_str());
        return GetDefault();
    }
    json j; f >> j;

    auto mat = MakeScope<Material>();
    if (j.contains("albedoColor")) {
        auto c = j["albedoColor"];
        mat->albedoColor = vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c.size() > 3 ? c[3].get<float>() : 1.0f);
    }
    if (j.contains("emissiveColor")) {
        auto c = j["emissiveColor"];
        mat->emissiveColor = vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
    }
    mat->emissiveStrength = j.value("emissiveStrength", 1.0f);
    mat->metallic = j.value("metallic", 0.0f);
    mat->roughness = j.value("roughness", 0.8f);
    mat->albedoMap = j.value("albedoMap", std::string());
    mat->normalMap = j.value("normalMap", std::string());
    mat->metallicRoughnessMap = j.value("metallicRoughnessMap", std::string());
    mat->emissiveMap = j.value("emissiveMap", std::string());
    mat->doubleSided = j.value("doubleSided", false);
    mat->transparent = j.value("transparent", false);
    mat->shaderAsset = j.value("shaderAsset", std::string("assets/shaders/Mesh.hlsl"));

    Material* raw = mat.get();
    m_Cache[path] = std::move(mat);
    return raw;
}

bool MaterialSystem::Save(const std::string& path, const Material& mat) {
    json j;
    j["albedoColor"] = { mat.albedoColor.r, mat.albedoColor.g, mat.albedoColor.b, mat.albedoColor.a };
    j["emissiveColor"] = { mat.emissiveColor.r, mat.emissiveColor.g, mat.emissiveColor.b };
    j["emissiveStrength"] = mat.emissiveStrength;
    j["metallic"] = mat.metallic;
    j["roughness"] = mat.roughness;
    j["albedoMap"] = mat.albedoMap;
    j["normalMap"] = mat.normalMap;
    j["metallicRoughnessMap"] = mat.metallicRoughnessMap;
    j["emissiveMap"] = mat.emissiveMap;
    j["doubleSided"] = mat.doubleSided;
    j["transparent"] = mat.transparent;
    j["shaderAsset"] = mat.shaderAsset;

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(2);
    return true;
}

#if FW_PLATFORM_WINDOWS
void MaterialSystem::Bind(ID3D11DeviceContext* ctx, ID3D11Buffer* materialCB, const Material& mat) {
    struct MaterialCBData {
        vec4 albedoColor;
        vec3 emissiveColor;
        float metallic;
        float roughness;
        float emissiveStrength;
        int hasAlbedoMap;
        int hasNormalMap;
        int hasMetallicRoughnessMap;
        int hasEmissiveMap;
        vec3 pad;
    } data{};

    data.albedoColor = mat.albedoColor;
    data.emissiveColor = mat.emissiveColor;
    data.metallic = mat.metallic;
    data.roughness = mat.roughness;
    data.emissiveStrength = mat.emissiveStrength;
    data.hasAlbedoMap = !mat.albedoMap.empty();
    data.hasNormalMap = !mat.normalMap.empty();
    data.hasMetallicRoughnessMap = !mat.metallicRoughnessMap.empty();
    data.hasEmissiveMap = !mat.emissiveMap.empty();

    ctx->UpdateSubresource(materialCB, 0, nullptr, &data, 0, 0);
    ctx->PSSetConstantBuffers(3, 1, &materialCB);

    GpuTexture* albedo = !mat.albedoMap.empty() ? m_Textures->Load(mat.albedoMap) : nullptr;
    GpuTexture* normal = !mat.normalMap.empty() ? m_Textures->Load(mat.normalMap) : nullptr;
    GpuTexture* mr = !mat.metallicRoughnessMap.empty() ? m_Textures->Load(mat.metallicRoughnessMap) : nullptr;
    GpuTexture* emissive = !mat.emissiveMap.empty() ? m_Textures->Load(mat.emissiveMap) : nullptr;

    ID3D11ShaderResourceView* srvs[4] = {
        (albedo ? albedo : m_Textures->GetWhiteTexture())->srv.Get(),
        (normal ? normal : m_Textures->GetFlatNormalTexture())->srv.Get(),
        (mr ? mr : m_Textures->GetWhiteTexture())->srv.Get(),
        (emissive ? emissive : m_Textures->GetWhiteTexture())->srv.Get(),
    };
    ctx->PSSetShaderResources(0, 4, srvs);
}
#endif

} // namespace fw
