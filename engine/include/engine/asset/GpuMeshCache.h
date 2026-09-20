#pragma once
// Uploads MeshData (CPU) to D3D11 vertex/index buffers and caches by path so
// repeated MeshRendererComponent::meshAsset references share GPU resources.
// Also watches for on-disk changes so the editor can hot-reload meshes
// (re-exported OBJ, recompiled brush) without restarting.

#include "engine/core/Base.h"
#include "engine/asset/MeshData.h"
#include <string>
#include <unordered_map>

#if FW_PLATFORM_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace fw {

class RenderDevice;

struct GpuMesh {
#if FW_PLATFORM_WINDOWS
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
#endif
    u32 indexCount = 0;
    std::vector<SubMesh> subMeshes;
    std::vector<std::string> materialSlotNames;
    AABB bounds;
};

class GpuMeshCache {
public:
    explicit GpuMeshCache(RenderDevice* device) : m_Device(device) {}

    GpuMesh* Load(const std::string& path);
    GpuMesh* GetOrCreatePrimitive(const std::string& key, const MeshData& data);
    void Invalidate(const std::string& path); // force re-upload next Load()
    void Clear();

private:
    GpuMesh* Upload(const std::string& key, const MeshData& data);

    RenderDevice* m_Device;
    std::unordered_map<std::string, Scope<GpuMesh>> m_Cache;
};

} // namespace fw
