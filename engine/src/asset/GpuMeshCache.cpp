#include "engine/asset/GpuMeshCache.h"
#include "engine/render/RenderDevice.h"
#include "engine/core/Log.h"

#if FW_PLATFORM_WINDOWS

namespace fw {

GpuMesh* GpuMeshCache::Upload(const std::string& key, const MeshData& data) {
    auto mesh = MakeScope<GpuMesh>();
    mesh->indexCount = (u32)data.indices.size();
    mesh->subMeshes = data.subMeshes;
    mesh->materialSlotNames = data.materialSlotNames;
    mesh->bounds = data.bounds;

    D3D11_BUFFER_DESC vbDesc{};
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.ByteWidth = (UINT)(sizeof(Vertex) * data.vertices.size());
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData{ data.vertices.data() };
    HRESULT hr = m_Device->Device()->CreateBuffer(&vbDesc, &vbData, &mesh->vertexBuffer);
    if (FAILED(hr)) { FW_LOG_ERROR("Failed to create vertex buffer for %s", key.c_str()); return nullptr; }

    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.ByteWidth = (UINT)(sizeof(u32) * data.indices.size());
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData{ data.indices.data() };
    hr = m_Device->Device()->CreateBuffer(&ibDesc, &ibData, &mesh->indexBuffer);
    if (FAILED(hr)) { FW_LOG_ERROR("Failed to create index buffer for %s", key.c_str()); return nullptr; }

    GpuMesh* raw = mesh.get();
    m_Cache[key] = std::move(mesh);
    return raw;
}

GpuMesh* GpuMeshCache::Load(const std::string& path) {
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second.get();

    // MeshData::LoadAny resolves project-relative paths (see core/Paths.h) and
    // understands the built-in primitives ("builtin:cube", ...), so the editor
    // can render geometry without any model files on disk.
    MeshData data;
    std::string err;
    if (!MeshData::LoadAny(path, data, &err)) {
        FW_LOG_ERROR("Failed to load mesh '%s': %s", path.c_str(), err.empty() ? "unknown error" : err.c_str());
        return nullptr;
    }
    return Upload(path, data);
}

GpuMesh* GpuMeshCache::GetOrCreatePrimitive(const std::string& key, const MeshData& data) {
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();
    return Upload(key, data);
}

void GpuMeshCache::Invalidate(const std::string& path) {
    m_Cache.erase(path);
}

void GpuMeshCache::Clear() { m_Cache.clear(); }

} // namespace fw

#endif
