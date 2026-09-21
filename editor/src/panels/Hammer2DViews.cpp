// Hammer mode's 2D orthographic views: the Top / Front / Side grid panes that,
// together with the 3D camera view, make up the classic Valve Hammer four-view
// layout (see the reference screenshot in the issue).
//
// Before this existed, "Hammer Mode" only toggled a single properties panel -
// the window kept the plain Godot-style single-viewport layout, so none of the
// UI a Hammer user expects (the 2D grids, the tool palette, the orthographic
// brush wireframes) was ever on screen.
//
// These views are drawn with ImGui draw lists rather than the 3D renderer: a
// 2D grid + brush wireframes is exactly what Hammer's ortho panes are, it needs
// no GPU, and it works identically on the CPU presentation path and in the
// headless screenshot tool.
//
// Engine axes are Y-up, so the Hammer panes map to:
//    Top   (X/Z) - looking down  -Y
//    Front (X/Y) - looking along -Z
//    Side  (Z/Y) - looking along -X
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace fw {

namespace {

struct AxisMap {
    const char* title;
    int horizontal;      // index into vec3 drawn along +X of the pane
    int vertical;        // index into vec3 drawn along +Y (up) of the pane
    bool flipHorizontal;
    bool flipVertical;
};

const AxisMap kViews[3] = {
    // Top: X right, Z *down* the screen (looking down the -Y axis).
    { "Top (X/Z)",   0, 2, false, false },
    { "Front (X/Y)", 0, 1, false, true  },
    { "Side (Z/Y)",  2, 1, true,  true  },
};

// Nice 1-2-5-10 grid spacing for the current zoom, like Hammer's grid steps.
float GridStepFor(float pixelsPerUnit) {
    const float targetPixels = 48.0f;
    float step = targetPixels / std::max(pixelsPerUnit, 1e-6f);
    float pow10 = std::pow(10.0f, std::floor(std::log10(std::max(step, 1e-6f))));
    const float n = step / pow10;
    if (n < 1.5f) return pow10;
    if (n < 3.5f) return 2.0f * pow10;
    if (n < 7.5f) return 5.0f * pow10;
    return 10.0f * pow10;
}

} // namespace

void EditorApp::DrawHammer2DViews() {
    for (int v = 0; v < 3; v++) {
        const AxisMap& map = kViews[v];
        HammerViewState& state = m_HammerViews[v];

        ImGui::Begin(map.title);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x < 32.0f) size.x = 32.0f;
        if (size.y < 32.0f) size.y = 32.0f;

        // An invisible button gives us hover/drag handling over the whole pane.
        ImGui::InvisibleButton("##pane", size,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);

        // ---- navigation: wheel zooms about the cursor, RMB/MMB drag pans ----
        ImGuiIO& io = ImGui::GetIO();
        if (hovered && io.MouseWheel != 0.0f) {
            const float oldZoom = state.zoom;
            state.zoom = glm::clamp(state.zoom * std::pow(1.15f, io.MouseWheel), 0.25f, 512.0f);
            // Keep the world point under the cursor pinned while zooming.
            const ImVec2 m = io.MousePos;
            const float dx = (m.x - center.x), dy = (m.y - center.y);
            state.panX += dx / oldZoom - dx / state.zoom;
            state.panY += dy / oldZoom - dy / state.zoom;
        }
        if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                        ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            const ImVec2 d = ImGui::IsMouseDragging(ImGuiMouseButton_Right)
                                 ? ImGui::GetMouseDragDelta(ImGuiMouseButton_Right)
                                 : ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            state.panX -= d.x / state.zoom;
            state.panY -= d.y / state.zoom;
            ImGui::ResetMouseDragDelta(ImGui::IsMouseDragging(ImGuiMouseButton_Right)
                                           ? ImGuiMouseButton_Right
                                           : ImGuiMouseButton_Middle);
        }

        const float ppu = state.zoom;
        auto worldToScreen = [&](const vec3& w) {
            float h = w[map.horizontal];
            float vv = w[map.vertical];
            if (map.flipHorizontal) h = -h;
            if (map.flipVertical) vv = -vv;
            return ImVec2(center.x + (h - state.panX) * ppu,
                          center.y + (vv - state.panY) * ppu);
        };
        auto screenToWorldAxes = [&](const ImVec2& s, float& outH, float& outV) {
            outH = (s.x - center.x) / ppu + state.panX;
            outV = (s.y - center.y) / ppu + state.panY;
            if (map.flipHorizontal) outH = -outH;
            if (map.flipVertical) outV = -outV;
        };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

        // ---- background + grid ---------------------------------------------
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 12, 14, 255));

        const float step = GridStepFor(ppu);
        const float majorEvery = 8.0f;

        // Visible world range along each pane axis.
        float h0, v0, h1, v1;
        screenToWorldAxes(origin, h0, v0);
        screenToWorldAxes(ImVec2(origin.x + size.x, origin.y + size.y), h1, v1);
        if (h0 > h1) std::swap(h0, h1);
        if (v0 > v1) std::swap(v0, v1);

        const int maxLines = 4000; // guard against absurd zoom-outs
        if ((h1 - h0) / step < maxLines && (v1 - v0) / step < maxLines) {
            for (float g = std::floor(h0 / step) * step; g <= h1; g += step) {
                vec3 a(0.0f), b(0.0f);
                a[map.horizontal] = g; a[map.vertical] = v0;
                b[map.horizontal] = g; b[map.vertical] = v1;
                const bool major = std::fabs(std::fmod(g / step, majorEvery)) < 0.001f;
                dl->AddLine(worldToScreen(a), worldToScreen(b),
                            major ? IM_COL32(52, 56, 62, 255) : IM_COL32(30, 32, 36, 255));
            }
            for (float g = std::floor(v0 / step) * step; g <= v1; g += step) {
                vec3 a(0.0f), b(0.0f);
                a[map.vertical] = g; a[map.horizontal] = h0;
                b[map.vertical] = g; b[map.horizontal] = h1;
                const bool major = std::fabs(std::fmod(g / step, majorEvery)) < 0.001f;
                dl->AddLine(worldToScreen(a), worldToScreen(b),
                            major ? IM_COL32(52, 56, 62, 255) : IM_COL32(30, 32, 36, 255));
            }
        }

        // World axes through the origin.
        {
            vec3 a(0.0f), b(0.0f);
            a[map.horizontal] = h0; b[map.horizontal] = h1;
            dl->AddLine(worldToScreen(a), worldToScreen(b), IM_COL32(90, 70, 70, 255));
            vec3 c(0.0f), d(0.0f);
            c[map.vertical] = v0; d[map.vertical] = v1;
            dl->AddLine(worldToScreen(c), worldToScreen(d), IM_COL32(70, 70, 100, 255));
        }

        // ---- brushes: wireframe of every face polygon ----------------------
        for (const auto& brush : m_Engine.GetBrushMap().Brushes()) {
            const bool selected = (brush.id == m_SelectedBrush) && m_SelectedBrush != UUID{0};
            const ImU32 col = selected ? IM_COL32(255, 220, 90, 255) : IM_COL32(220, 90, 220, 255);
            for (const auto& face : brush.faces) {
                if (face.polygon.size() < 2) continue;
                for (size_t i = 0; i < face.polygon.size(); i++) {
                    const vec3& p0 = face.polygon[i];
                    const vec3& p1 = face.polygon[(i + 1) % face.polygon.size()];
                    dl->AddLine(worldToScreen(p0), worldToScreen(p1), col, selected ? 2.0f : 1.0f);
                }
            }
        }

        // ---- mesh entities: bounding rectangle + name ----------------------
        m_Engine.GetScene().Each<TransformComponent, MeshRendererComponent>(
            [&](Entity e, TransformComponent& tc, MeshRendererComponent&) {
                const vec3 p = vec3(tc.worldMatrix[3]);
                const vec3 s = tc.local.scale;
                const bool selected = (e.Handle() == m_SelectedEntity);
                const ImU32 col = selected ? IM_COL32(255, 220, 90, 255) : IM_COL32(110, 200, 255, 220);

                vec3 lo = p, hi = p;
                lo[map.horizontal] -= 0.5f * std::fabs(s[map.horizontal]);
                hi[map.horizontal] += 0.5f * std::fabs(s[map.horizontal]);
                lo[map.vertical] -= 0.5f * std::fabs(s[map.vertical]);
                hi[map.vertical] += 0.5f * std::fabs(s[map.vertical]);

                const ImVec2 a = worldToScreen(lo), b = worldToScreen(hi);
                dl->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                            ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
                            col, 0.0f, 0, selected ? 2.0f : 1.0f);
                // Only label a box that is actually wide enough to hold the
                // text, otherwise a cluster of small props turns into a pile of
                // unreadable overlapping names. The selection always gets its
                // label, drawn above the box so it never sits on the geometry.
                const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
                const float boxW = std::fabs(b.x - a.x);
                const char* name = e.Name().c_str();
                const float textW = ImGui::CalcTextSize(name).x;
                if (selected || (ppu > 6.0f && textW + 6.0f <= boxW))
                    dl->AddText(ImVec2(x0 + 2.0f, y0 - ImGui::GetTextLineHeight() - 1.0f), col, name);
            });

        // ---- lights / cameras as Hammer-style point-entity markers ---------
        m_Engine.GetScene().Each<TransformComponent, LightComponent>(
            [&](Entity e, TransformComponent& tc, LightComponent&) {
                const ImVec2 s = worldToScreen(vec3(tc.worldMatrix[3]));
                const ImU32 col = (e.Handle() == m_SelectedEntity) ? IM_COL32(255, 220, 90, 255)
                                                                   : IM_COL32(255, 210, 120, 220);
                dl->AddCircle(s, 6.0f, col, 12);
                dl->AddLine(ImVec2(s.x - 8, s.y), ImVec2(s.x + 8, s.y), col);
                dl->AddLine(ImVec2(s.x, s.y - 8), ImVec2(s.x, s.y + 8), col);
            });
        m_Engine.GetScene().Each<TransformComponent, CameraComponent>(
            [&](Entity e, TransformComponent& tc, CameraComponent&) {
                const ImVec2 s = worldToScreen(vec3(tc.worldMatrix[3]));
                const ImU32 col = (e.Handle() == m_SelectedEntity) ? IM_COL32(255, 220, 90, 255)
                                                                   : IM_COL32(140, 255, 160, 220);
                dl->AddRect(ImVec2(s.x - 6, s.y - 5), ImVec2(s.x + 6, s.y + 5), col);
            });

        // The 3D fly camera's position, so the panes and the 3D view relate.
        {
            const ImVec2 s = worldToScreen(m_CamPos);
            dl->AddCircleFilled(s, 4.0f, IM_COL32(120, 220, 255, 255));
            dl->AddCircle(s, 8.0f, IM_COL32(120, 220, 255, 160), 16);
        }

        // ---- click to select a brush in this pane --------------------------
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float mh, mv;
            screenToWorldAxes(io.MousePos, mh, mv);
            UUID hit{0};
            float bestArea = 0.0f;
            for (const auto& brush : m_Engine.GetBrushMap().Brushes()) {
                const AABB bounds = brush.Bounds();
                if (!bounds.Valid()) continue;
                if (mh < bounds.min[map.horizontal] || mh > bounds.max[map.horizontal]) continue;
                if (mv < bounds.min[map.vertical] || mv > bounds.max[map.vertical]) continue;
                const float area = (bounds.max[map.horizontal] - bounds.min[map.horizontal]) *
                                   (bounds.max[map.vertical] - bounds.min[map.vertical]);
                if (hit == UUID{0} || area < bestArea) { hit = brush.id; bestArea = area; }
            }
            m_SelectedBrush = hit;
            m_SoftwarePreviewDirty = true;
        }

        // ---- HUD: axis label, grid size, cursor position -------------------
        // The pane's dock tab already names the view, so the HUD only carries
        // the numbers that change as you navigate.
        char hud[128];
        std::snprintf(hud, sizeof(hud), "grid %.2f   zoom %.2f px/u", step, ppu);
        dl->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), IM_COL32(200, 200, 210, 255), hud);
        if (hovered) {
            float mh, mv;
            screenToWorldAxes(io.MousePos, mh, mv);
            std::snprintf(hud, sizeof(hud), "%.2f, %.2f", mh, mv);
            dl->AddText(ImVec2(origin.x + 6.0f, origin.y + size.y - 18.0f),
                        IM_COL32(200, 200, 210, 255), hud);
        }

        dl->PopClipRect();
        ImGui::End();
    }
}

} // namespace fw
