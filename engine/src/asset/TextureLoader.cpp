#include "engine/asset/TextureLoader.h"
#include "engine/render/RenderDevice.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if FW_PLATFORM_WINDOWS

namespace fw {

GpuTexture* TextureLoader::UploadRGBA8(const std::string& key, const unsigned char* pixels, int width, int height) {
    auto tex = MakeScope<GpuTexture>();
    tex->width = width;
    tex->height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr = m_Device->Device()->CreateTexture2D(&desc, nullptr, &tex->texture);
    if (FAILED(hr)) {
        FW_LOG_ERROR("Failed to create texture2D for %s", key.c_str());
        return nullptr;
    }

    m_Device->Context()->UpdateSubresource(tex->texture.Get(), 0, nullptr, pixels, width * 4, 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = (UINT)-1;
    m_Device->Device()->CreateShaderResourceView(tex->texture.Get(), &srvDesc, &tex->srv);
    m_Device->Context()->GenerateMips(tex->srv.Get());

    GpuTexture* raw = tex.get();
    m_Cache[key] = std::move(tex);
    return raw;
}

GpuTexture* TextureLoader::Load(const std::string& path) {
    auto it = m_Cache.find(path);
    if (it != m_Cache.end()) return it->second.get();

    const std::string resolved = Paths::Resolve(path);
    int w, h, channels;
    unsigned char* data = stbi_load(resolved.c_str(), &w, &h, &channels, 4);
    if (!data) {
        FW_LOG_WARN("Failed to load texture: %s (%s) - using the dev checker texture",
                    resolved.c_str(), stbi_failure_reason());
        return GetErrorTexture();
    }
    GpuTexture* result = UploadRGBA8(path, data, w, h);
    stbi_image_free(data);
    return result;
}

GpuTexture* TextureLoader::GetWhiteTexture() {
    const std::string key = "__white__";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();
    unsigned char px[4] = { 255, 255, 255, 255 };
    return UploadRGBA8(key, px, 1, 1);
}

GpuTexture* TextureLoader::GetFlatNormalTexture() {
    const std::string key = "__flatnormal__";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();
    unsigned char px[4] = { 128, 128, 255, 255 };
    return UploadRGBA8(key, px, 1, 1);
}

GpuTexture* TextureLoader::GetErrorTexture() {
    const std::string key = "__error__";
    auto it = m_Cache.find(key);
    if (it != m_Cache.end()) return it->second.get();

    const int size = 16;
    std::vector<unsigned char> pixels(size * size * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool checker = ((x / 4) + (y / 4)) % 2 == 0;
            unsigned char* p = &pixels[(y * size + x) * 4];
            p[0] = checker ? 255 : 0; p[1] = 0; p[2] = checker ? 255 : 0; p[3] = 255;
        }
    }
    return UploadRGBA8(key, pixels.data(), size, size);
}

void TextureLoader::Clear() { m_Cache.clear(); }

} // namespace fw

#endif
