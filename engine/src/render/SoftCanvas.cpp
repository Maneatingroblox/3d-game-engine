#include "engine/render/SoftCanvas.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fw {

namespace {

inline u8 ColR(ImU32 c) { return (u8)((c >> IM_COL32_R_SHIFT) & 0xFF); }
inline u8 ColG(ImU32 c) { return (u8)((c >> IM_COL32_G_SHIFT) & 0xFF); }
inline u8 ColB(ImU32 c) { return (u8)((c >> IM_COL32_B_SHIFT) & 0xFF); }
inline u8 ColA(ImU32 c) { return (u8)((c >> IM_COL32_A_SHIFT) & 0xFF); }

inline float Edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

inline u8 Round255(float v) {
    const float c = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
    return (u8)(c + 0.5f);
}

} // namespace

void SoftCanvas::Resize(int width, int height) {
    m_Width = width > 0 ? width : 0;
    m_Height = height > 0 ? height : 0;
    m_Pixels.assign((size_t)m_Width * (size_t)m_Height * 4u, 0);
    Clear(IM_COL32(13, 13, 16, 255));
}

void SoftCanvas::Clear(u32 rgba) {
    if (Empty()) return;
    u8* p = m_Pixels.data();
    const u8 r = ColR(rgba), g = ColG(rgba), b = ColB(rgba), a = ColA(rgba);
    const size_t count = (size_t)m_Width * (size_t)m_Height;
    for (size_t i = 0; i < count; i++) {
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        p += 4;
    }
}

unsigned int SoftCanvas::CreateTexture(const u8* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return 0;
    Texture& tex = m_Textures[m_NextTextureId];
    tex.width = width;
    tex.height = height;
    tex.bytesPerPixel = 4;
    tex.pixels.assign(rgba, rgba + (size_t)width * (size_t)height * 4u);
    return m_NextTextureId++;
}

void SoftCanvas::UpdateTexture(unsigned int id, const u8* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return;
    auto it = m_Textures.find(id);
    if (it == m_Textures.end()) { CreateTexture(rgba, width, height); return; }
    Texture& tex = it->second;
    tex.width = width;
    tex.height = height;
    tex.bytesPerPixel = 4;
    tex.pixels.assign(rgba, rgba + (size_t)width * (size_t)height * 4u);
}

void SoftCanvas::DestroyTexture(unsigned int id) { m_Textures.erase(id); }

const SoftCanvas::Texture* SoftCanvas::FindTexture(ImTextureID id) const {
    const unsigned int key = (unsigned int)(size_t)id;
    if (key == 0) return nullptr;
    auto it = m_Textures.find(key);
    return it == m_Textures.end() ? nullptr : &it->second;
}

// Copies a rectangle of source pixels into an existing texture (used for the
// incremental glyph updates ImGui sends for the font atlas).
void SoftCanvas::BlitPixels(int id, const u8* src, int srcPitch, int width, int height, int dstX, int dstY) {
    auto it = m_Textures.find((unsigned int)id);
    if (it == m_Textures.end()) return;
    Texture& tex = it->second;
    for (int y = 0; y < height; y++) {
        const int dy = dstY + y;
        if (dy < 0 || dy >= tex.height) continue;
        for (int x = 0; x < width; x++) {
            const int dx = dstX + x;
            if (dx < 0 || dx >= tex.width) continue;
            const u8* s = src + (size_t)y * (size_t)srcPitch + (size_t)x * (size_t)tex.bytesPerPixel;
            u8* d = tex.pixels.data() + ((size_t)dy * (size_t)tex.width + (size_t)dx) * (size_t)tex.bytesPerPixel;
            std::memcpy(d, s, (size_t)tex.bytesPerPixel);
        }
    }
}

void SoftCanvas::SyncTextures(ImVector<ImTextureData*>* textures) {
    if (!textures) return;
    for (int n = 0; n < textures->Size; n++) {
        ImTextureData* tex = (*textures)[n];
        if (!tex) continue;

        switch (tex->Status) {
            case ImTextureStatus_WantCreate: {
                const int bpp = tex->BytesPerPixel > 0 ? tex->BytesPerPixel : 4;
                const u8* src = (const u8*)tex->GetPixels();
                const unsigned int id = m_NextTextureId++;
                Texture& dst = m_Textures[id];
                dst.width = tex->Width;
                dst.height = tex->Height;
                dst.bytesPerPixel = bpp;
                dst.pixels.assign(src, src + (size_t)tex->Width * (size_t)tex->Height * (size_t)bpp);
                tex->SetTexID((ImTextureID)id);
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantUpdates: {
                const unsigned int id = (unsigned int)(size_t)tex->TexID;
                if (tex->Updates.Size > 0) {
                    for (int u = 0; u < tex->Updates.Size; u++) {
                        const ImTextureRect& r = tex->Updates[u];
                        const u8* src = (const u8*)tex->GetPixelsAt(r.x, r.y);
                        BlitPixels((int)id, src, tex->GetPitch(), r.w, r.h, r.x, r.y);
                    }
                } else if (tex->UpdateRect.w > 0 && tex->UpdateRect.h > 0) {
                    const ImTextureRect& r = tex->UpdateRect;
                    const u8* src = (const u8*)tex->GetPixelsAt(r.x, r.y);
                    BlitPixels((int)id, src, tex->GetPitch(), r.w, r.h, r.x, r.y);
                }
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantDestroy: {
                const unsigned int id = (unsigned int)(size_t)tex->TexID;
                if (id != 0) m_Textures.erase(id);
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            }
            default:
                break;
        }
    }
}

u32 SoftCanvas::SampleTexel(const Texture& tex, float u, float v) const {
    if (tex.width <= 0 || tex.height <= 0) return IM_COL32(255, 255, 255, 255);

    // Bilinear, clamped. ImGui UI coordinates are pixel centres, so shift by
    // half a texel before sampling.
    float fx = u * (float)tex.width - 0.5f;
    float fy = v * (float)tex.height - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float tx = fx - (float)x0, ty = fy - (float)y0;

    auto fetch = [&](int x, int y) -> u32 {
        x = std::clamp(x, 0, tex.width - 1);
        y = std::clamp(y, 0, tex.height - 1);
        const u8* p = tex.pixels.data() + ((size_t)y * (size_t)tex.width + (size_t)x) * (size_t)tex.bytesPerPixel;
        if (tex.bytesPerPixel == 1) return IM_COL32(255, 255, 255, p[0]);
        return IM_COL32(p[0], p[1], p[2], p[3]);
    };

    const u32 c00 = fetch(x0, y0), c10 = fetch(x0 + 1, y0);
    const u32 c01 = fetch(x0, y0 + 1), c11 = fetch(x0 + 1, y0 + 1);

    auto mix = [&](int shift) {
        const float a = (float)((c00 >> shift) & 0xFF) * (1 - tx) + (float)((c10 >> shift) & 0xFF) * tx;
        const float b = (float)((c01 >> shift) & 0xFF) * (1 - tx) + (float)((c11 >> shift) & 0xFF) * tx;
        return (u32)(a * (1 - ty) + b * ty + 0.5f);
    };
    return (mix(IM_COL32_R_SHIFT) << IM_COL32_R_SHIFT) |
           (mix(IM_COL32_G_SHIFT) << IM_COL32_G_SHIFT) |
           (mix(IM_COL32_B_SHIFT) << IM_COL32_B_SHIFT) |
           (mix(IM_COL32_A_SHIFT) << IM_COL32_A_SHIFT);
}

void SoftCanvas::RasterizeTriangle(const ImDrawVert& va, const ImDrawVert& vb, const ImDrawVert& vc,
                                   const Texture* tex, int clipX0, int clipY0, int clipX1, int clipY1) {
    ImDrawVert v0 = va, v1 = vb, v2 = vc;
    float area = Edge(v0.pos.x, v0.pos.y, v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y);
    if (area == 0.0f) return;
    if (area < 0.0f) { std::swap(v1, v2); area = -area; }

    const float minXf = std::min(v0.pos.x, std::min(v1.pos.x, v2.pos.x));
    const float maxXf = std::max(v0.pos.x, std::max(v1.pos.x, v2.pos.x));
    const float minYf = std::min(v0.pos.y, std::min(v1.pos.y, v2.pos.y));
    const float maxYf = std::max(v0.pos.y, std::max(v1.pos.y, v2.pos.y));

    const int x0 = std::max(clipX0, (int)std::floor(minXf));
    const int x1 = std::min(clipX1, (int)std::ceil(maxXf));
    const int y0 = std::max(clipY0, (int)std::floor(minYf));
    const int y1 = std::min(clipY1, (int)std::ceil(maxYf));
    if (x0 >= x1 || y0 >= y1) return;

    const float invArea = 1.0f / area;
    const float cr0 = ColR(v0.col), cg0 = ColG(v0.col), cb0 = ColB(v0.col), ca0 = ColA(v0.col);
    const float cr1 = ColR(v1.col), cg1 = ColG(v1.col), cb1 = ColB(v1.col), ca1 = ColA(v1.col);
    const float cr2 = ColR(v2.col), cg2 = ColG(v2.col), cb2 = ColB(v2.col), ca2 = ColA(v2.col);

    for (int y = y0; y < y1; y++) {
        const float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            const float px = (float)x + 0.5f;
            const float w0 = Edge(v1.pos.x, v1.pos.y, v2.pos.x, v2.pos.y, px, py) * invArea;
            if (w0 < 0.0f) continue;
            const float w1 = Edge(v2.pos.x, v2.pos.y, v0.pos.x, v0.pos.y, px, py) * invArea;
            if (w1 < 0.0f) continue;
            const float w2 = 1.0f - w0 - w1;
            if (w2 < 0.0f) continue;

            const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
            const float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;

            // Missing texture: draw an obvious magenta/black checker instead of
            // silently producing an empty area (that is what made "the editor is
            // blank" impossible to interpret).
            u32 texel;
            if (tex) {
                texel = SampleTexel(*tex, u, v);
            } else {
                texel = ((x / 8 + y / 8) & 1) ? IM_COL32(255, 0, 255, 255) : IM_COL32(0, 0, 0, 255);
            }

            const float sr = (w0 * cr0 + w1 * cr1 + w2 * cr2) / 255.0f * (float)ColR(texel) / 255.0f;
            const float sg = (w0 * cg0 + w1 * cg1 + w2 * cg2) / 255.0f * (float)ColG(texel) / 255.0f;
            const float sb = (w0 * cb0 + w1 * cb1 + w2 * cb2) / 255.0f * (float)ColB(texel) / 255.0f;
            const float sa = (w0 * ca0 + w1 * ca1 + w2 * ca2) / 255.0f * (float)ColA(texel) / 255.0f;
            if (sa <= 0.0f) continue;

            // Same blend the D3D11 backend uses for ImGui:
            //   rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
            //   a   = src.a       + dst.a   * (1 - src.a)
            u8* dst = m_Pixels.data() + ((size_t)y * (size_t)m_Width + (size_t)x) * 4u;
            const float ia = 1.0f - sa;
            dst[0] = Round255(sr * sa * 255.0f + (float)dst[0] * ia);
            dst[1] = Round255(sg * sa * 255.0f + (float)dst[1] * ia);
            dst[2] = Round255(sb * sa * 255.0f + (float)dst[2] * ia);
            dst[3] = Round255(sa * 255.0f + (float)dst[3] * ia);
        }
    }
}

void SoftCanvas::RenderDrawData(ImDrawData* drawData) {
    if (!drawData || !drawData->Valid || Empty()) return;

    const ImVec2 disp = drawData->DisplayPos;
    const ImVec2 scale = drawData->FramebufferScale;
    const int fbW = m_Width, fbH = m_Height;

    for (int n = 0; n < drawData->CmdListsCount; n++) {
        const ImDrawList* list = drawData->CmdLists[n];
        if (!list) continue;
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;

        for (int c = 0; c < list->CmdBuffer.Size; c++) {
            const ImDrawCmd& cmd = list->CmdBuffer[c];
            if (cmd.UserCallback != nullptr) continue;  // no callbacks are used by the editor UI
            if (cmd.ElemCount == 0) continue;

            const Texture* tex = FindTexture(cmd.GetTexID());

            int cx0 = (int)std::floor((cmd.ClipRect.x - disp.x) * scale.x);
            int cy0 = (int)std::floor((cmd.ClipRect.y - disp.y) * scale.y);
            int cx1 = (int)std::ceil((cmd.ClipRect.z - disp.x) * scale.x);
            int cy1 = (int)std::ceil((cmd.ClipRect.w - disp.y) * scale.y);
            cx0 = std::clamp(cx0, 0, fbW); cy1 = std::clamp(cy1, 0, fbH);
            cy0 = std::clamp(cy0, 0, fbH); cx1 = std::clamp(cx1, 0, fbW);
            if (cx0 >= cx1 || cy0 >= cy1) continue;

            // Mirror imgui_impl_dx11's DrawIndexed call: the vertex buffer is
            // indexed from cmd.VtxOffset and the index buffer from cmd.IdxOffset
            // (both relative to this draw list's own buffers).
            const ImDrawIdx* indices = idx + cmd.IdxOffset;
            for (unsigned int e = 0; e + 2 < cmd.ElemCount; e += 3) {
                const ImDrawVert& a = vtx[indices[e + 0] + cmd.VtxOffset];
                const ImDrawVert& b = vtx[indices[e + 1] + cmd.VtxOffset];
                const ImDrawVert& d = vtx[indices[e + 2] + cmd.VtxOffset];
                RasterizeTriangle(a, b, d, tex, cx0, cy0, cx1, cy1);
            }
        }
    }
}

void SoftCanvas::RenderImGuiFrame() {
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;
    ImVector<ImTextureData*>* textures = drawData->Textures ? drawData->Textures : &ImGui::GetPlatformIO().Textures;
    SyncTextures(textures);
    RenderDrawData(drawData);
}

} // namespace fw
