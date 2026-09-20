#pragma once
// Loads PNG/JPG/TGA/BMP textures (via stb_image) straight into a D3D11
// Texture2D + SRV, with automatic mip generation. Cached by path so the same
// texture asset is never uploaded twice.

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

struct GpuTexture {
#if FW_PLATFORM_WINDOWS
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
#endif
    int width = 0, height = 0;
};

class TextureLoader {
public:
    explicit TextureLoader(RenderDevice* device) : m_Device(device) {}

    // Returns a cached texture if already loaded; otherwise loads from disk.
    // Returns nullptr (and logs) on failure; callers should fall back to
    // GetWhiteTexture()/GetErrorTexture().
    GpuTexture* Load(const std::string& path);

    GpuTexture* GetWhiteTexture();
    GpuTexture* GetErrorTexture(); // magenta/black checkerboard, like Source engine's missing texture
    GpuTexture* GetFlatNormalTexture();

    void Clear();

private:
    GpuTexture* UploadRGBA8(const std::string& key, const unsigned char* pixels, int width, int height);

    RenderDevice* m_Device;
    std::unordered_map<std::string, Scope<GpuTexture>> m_Cache;
};

} // namespace fw
