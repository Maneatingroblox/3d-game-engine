#pragma once
// SoftCanvas: an RGBA8 CPU framebuffer that can rasterize Dear ImGui draw data
// and host textures for ImGui::Image().
//
// Why this exists
// ---------------
// The Map Maker used to be able to show *nothing at all*: if the D3D11 device,
// the swap chain, a shader or the ImGui DX11 backend failed, the window opened
// and stayed blank with no way to tell what broke. This canvas removes the GPU
// from the equation: the editor can render the whole frame (CPU-rendered 3D
// viewport + the entire ImGui interface) and blit it to the window with plain
// GDI, and the exact same rasterizer renders the editor UI to a PNG in
// headless/CI builds (tools/ui_shot.cpp) so the interface can be inspected
// without a display.
//
// It implements the Dear ImGui renderer-backend contract (dynamic textures plus
// ImDrawData rasterization with clipping and alpha blending), mirroring what
// engine/render + imgui_impl_dx11 do on the GPU.

#include "engine/core/Base.h"
#include <imgui.h>

#include <unordered_map>
#include <vector>

namespace fw {

class SoftCanvas {
public:
    void Resize(int width, int height);

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    int Pitch() const { return m_Width * 4; }
    bool Empty() const { return m_Width <= 0 || m_Height <= 0; }

    u8* Pixels() { return m_Pixels.data(); }
    const u8* Pixels() const { return m_Pixels.data(); }

    void Clear(u32 rgba);

    // ---- texture registry ---------------------------------------------------
    // Used for the editor's CPU viewport image (ImGui::Image()) and for the
    // font atlas. Ids start at 1 so 0 stays "no texture".
    unsigned int CreateTexture(const u8* rgba, int width, int height);
    void UpdateTexture(unsigned int id, const u8* rgba, int width, int height);
    void DestroyTexture(unsigned int id);
    bool HasTexture(unsigned int id) const { return m_Textures.count(id) != 0; }

    // ---- ImGui rendering ----------------------------------------------------
    // Honours the renderer-backend texture requests (font atlas creation and
    // incremental glyph updates) for the frame being rendered.
    void SyncTextures(ImVector<ImTextureData*>* textures);
    // Rasterizes ImGui::GetDrawData() into the canvas.
    void RenderDrawData(ImDrawData* drawData);
    // SyncTextures() + RenderDrawData() for the frame that was just rendered.
    void RenderImGuiFrame();

private:
    struct Texture {
        int width = 0;
        int height = 0;
        int bytesPerPixel = 4;   // 4 = RGBA32, 1 = Alpha8 (font atlas glyphs)
        std::vector<u8> pixels;  // top-down, tightly packed
    };

    const Texture* FindTexture(ImTextureID id) const;
    u32 SampleTexel(const Texture& tex, float u, float v) const;
    void BlitPixels(int id, const u8* src, int srcPitch, int width, int height, int dstX, int dstY);
    void RasterizeTriangle(const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2,
                           const Texture* tex, int clipX0, int clipY0, int clipX1, int clipY1);

    int m_Width = 0;
    int m_Height = 0;
    std::vector<u8> m_Pixels;
    std::unordered_map<unsigned int, Texture> m_Textures;
    unsigned int m_NextTextureId = 1;
};

} // namespace fw
