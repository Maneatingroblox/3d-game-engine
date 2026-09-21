// Hammer mode: Valve Hammer Editor-style brush authoring layered on top of
// the same ECS scene. Entering Hammer mode switches the whole GUI to the
// classic quad view - 3D camera (top-left) plus top / front / side
// orthographic views with grids - and a Hammer-style tool panel. New brushes
// are dragged out in any 2D view (Block tool), then edited with Clip (split a
// brush along a plane) and Carve (boolean-subtract one brush's volume from
// others). Every brush is immediately compiled into a MeshRendererComponent +
// ColliderComponent entity via BrushMap::CompileToScene, so Play mode, physics
// and scripts see brush geometry exactly like any hand-placed mesh.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/platform/Input.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fw {

// ---------------------------------------------------------------------------
// 2D orthographic views (Hammer's top / front / side panes)
// ---------------------------------------------------------------------------

// Each view shows the plane spanned by (right, up) world axes; +up is drawn
// towards the top of the panel.
void EditorApp::OrthoViewAxes(int view, vec3& right, vec3& up) {
    switch (view) {
        case 0:  right = vec3(1, 0, 0);  up = vec3(0, 0, -1); break; // top   (x/z, north up)
        case 1:  right = vec3(1, 0, 0);  up = vec3(0, 1, 0);  break; // front (x/y)
        default: right = vec3(0, 0, 1);  up = vec3(0, 1, 0);  break; // side  (z/y)
    }
}

void EditorApp::OrthoViewPlaneCoords(int view, const vec3& world, float& u, float& w) {
    vec3 right, up;
    OrthoViewAxes(view, right, up);
    u = glm::dot(world, right);
    w = glm::dot(world, up);
}

void EditorApp::OrthoViewWorldPoint(int view, const vec2& mouseScreenPos, const ImVec2& rectPos,
                                    const ImVec2& rectSize, vec3& outWorld) const {
    vec3 right, up;
    OrthoViewAxes(view, right, up);
    const OrthoView& ov = m_OrthoViews[view];
    const float cx = rectPos.x + rectSize.x * 0.5f;
    const float cy = rectPos.y + rectSize.y * 0.5f;
    const float u = ov.center.x + (mouseScreenPos.x - cx) / ov.zoom;
    const float w = ov.center.y - (mouseScreenPos.y - cy) / ov.zoom;
    outWorld = right * u + up * w;
}

void EditorApp::CreateBlockBrush(const vec3& mins, const vec3& maxs) {
    if (glm::all(glm::lessThanEqual(maxs, mins))) return;
    Brush b = Brush::CreateBox(mins, maxs, "assets/textures/dev/dev_grey.png");
    b.Rebuild();
    b.classname = "worldspawn";
    m_Engine.GetBrushMap().AddBrush(b);
    m_Engine.GetBrushMap().MarkAllDirty();
    m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
    m_SelectedBrush = b.id;
    m_SoftwarePreviewDirty = true;
    FW_LOG_INFO("Created brush (%.2f %.2f %.2f)..(%.2f %.2f %.2f)",
                mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
}

void EditorApp::HandleOrthoViewInput(int view, const ImVec2& rectPos, const ImVec2& rectSize) {
    const bool hovered = ImGui::IsWindowHovered();
    Input& input = Input::Get();
    OrthoView& ov = m_OrthoViews[view];

    // ---- pan: MMB drag (Hammer style) ------------------------------------
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        ov.center.x -= d.x / ov.zoom;
        ov.center.y += d.y / ov.zoom;
    }

    // ---- zoom: wheel, anchored on the cursor ------------------------------
    if (hovered && std::abs(input.WheelDelta()) > 0.0f) {
        const vec2 mpos = input.MousePosition();
        vec3 before;
        OrthoViewWorldPoint(view, mpos, rectPos, rectSize, before);
        float beforeU, beforeW;
        OrthoViewPlaneCoords(view, before, beforeU, beforeW);
        ov.zoom = glm::clamp(ov.zoom * std::pow(1.15f, input.WheelDelta()), 2.0f, 512.0f);
        // Keep the world point under the cursor fixed.
        const float cx = rectPos.x + rectSize.x * 0.5f;
        const float cy = rectPos.y + rectSize.y * 0.5f;
        ov.center.x = beforeU - (mpos.x - cx) / ov.zoom;
        ov.center.y = beforeW + (mpos.y - cy) / ov.zoom;
    }

    // ---- LMB: block dragging or brush picking ----------------------------
    const float snap = m_GridSnap > 0.0f ? m_GridSnap : 0.0f;
    auto snapped = [snap](float v) { return snap > 0.0f ? std::round(v / snap) * snap : v; };

    const vec2 mpos = input.MousePosition();
    vec3 wp;
    OrthoViewWorldPoint(view, mpos, rectPos, rectSize, wp);
    float u = 0.0f, w = 0.0f;
    OrthoViewPlaneCoords(view, wp, u, w);

    if (m_BrushTool == BrushTool::Block) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_BlockDragging = true;
            m_BlockDragView = view;
            m_BlockDragU0 = m_BlockDragU1 = snapped(u);
            m_BlockDragW0 = m_BlockDragW1 = snapped(w);
        }
        if (m_BlockDragging && m_BlockDragView == view) {
            m_BlockDragU1 = snapped(u);
            m_BlockDragW1 = snapped(w);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_BlockDragging = false;
                const float u0 = std::min(m_BlockDragU0, m_BlockDragU1);
                const float u1 = std::max(m_BlockDragU0, m_BlockDragU1);
                const float w0 = std::min(m_BlockDragW0, m_BlockDragW1);
                const float w1 = std::max(m_BlockDragW0, m_BlockDragW1);
                if (u1 - u0 > 1e-3f && w1 - w0 > 1e-3f) {
                    vec3 mins, maxs;
                    switch (view) {
                        case 0: // top: rect is X/Z, extrude Y from the Block tool params
                            mins = vec3(u0, m_BlockBaseY, -w1);
                            maxs = vec3(u1, m_BlockBaseY + m_BlockHeight, -w0);
                            break;
                        case 1: // front: rect is X/Y, extrude Z by Depth
                            mins = vec3(u0, w0, -m_BlockDepth * 0.5f);
                            maxs = vec3(u1, w1, m_BlockDepth * 0.5f);
                            break;
                        default: // side: rect is Z/Y, extrude X by Depth
                            mins = vec3(-m_BlockDepth * 0.5f, w0, u0);
                            maxs = vec3(m_BlockDepth * 0.5f, w1, u1);
                            break;
                    }
                    CreateBlockBrush(mins, maxs);
                }
            }
        }
    } else if (m_BrushTool == BrushTool::Select) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float closestArea = std::numeric_limits<float>::max();
            UUID hit{0};
            for (auto& brush : m_Engine.GetBrushMap().Brushes()) {
                AABB b = brush.Bounds();
                if (!b.Valid()) continue;
                float bu0, bu1, bw0, bw1;
                {
                    float ua, wa, ub, wb;
                    OrthoViewPlaneCoords(view, vec3(b.min.x, b.min.y, b.min.z), ua, wa);
                    OrthoViewPlaneCoords(view, vec3(b.max.x, b.max.y, b.max.z), ub, wb);
                    bu0 = std::min(ua, ub); bu1 = std::max(ua, ub);
                    bw0 = std::min(wa, wb); bw1 = std::max(wa, wb);
                }
                if (u >= bu0 && u <= bu1 && w >= bw0 && w <= bw1) {
                    const float area = (bu1 - bu0) * (bw1 - bw0);
                    if (area < closestArea) { closestArea = area; hit = brush.id; }
                }
            }
            m_SelectedBrush = hit;
            m_SoftwarePreviewDirty = true;
        }
    }
}

void EditorApp::DrawOrthoViewContent(int view, ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
    const OrthoView& ov = m_OrthoViews[view];
    vec3 right, up;
    OrthoViewAxes(view, right, up);
    const float cx = pos.x + size.x * 0.5f;
    const float cy = pos.y + size.y * 0.5f;
    auto toScreen = [&](const vec3& world) {
        float u, w;
        OrthoViewPlaneCoords(view, world, u, w);
        return ImVec2(cx + (u - ov.center.x) * ov.zoom, cy - (w - ov.center.y) * ov.zoom);
    };

    // ---- grid ------------------------------------------------------------
    float step = m_GridSnap > 0.0f ? m_GridSnap : 0.0f;
    if (step <= 0.0f) {
        // Auto grid: aim for ~24px cells.
        step = std::pow(2.0f, std::round(std::log2(24.0f / ov.zoom)));
        step = glm::clamp(step, 0.03125f, 64.0f);
    }
    const float uMin = ov.center.x - (size.x * 0.5f) / ov.zoom;
    const float uMax = ov.center.x + (size.x * 0.5f) / ov.zoom;
    const float wMin = ov.center.y - (size.y * 0.5f) / ov.zoom;
    const float wMax = ov.center.y + (size.y * 0.5f) / ov.zoom;
    const int majorEvery = 8;

    auto drawLineClipped = [&](float x0, float y0, float x1, float y1, ImU32 col) {
        dl->AddLine(ImVec2(pos.x + x0, pos.y + y0), ImVec2(pos.x + x1, pos.y + y1), col);
    };
    {
        const float pxPerStep = step * ov.zoom;
        int i0 = (int)std::floor(uMin / step), i1 = (int)std::ceil(uMax / step);
        for (int i = i0; i <= i1; i++) {
            const float u = i * step;
            const float x = cx + (u - ov.center.x) * ov.zoom - pos.x;
            const bool major = (i % majorEvery) == 0;
            ImU32 col = u == 0.0f ? IM_COL32(120, 160, 120, 200)
                       : major ? IM_COL32(70, 70, 80, 160) : IM_COL32(48, 48, 56, 110);
            drawLineClipped(x, 0, x, size.y, col);
        }
        i0 = (int)std::floor(wMin / step); i1 = (int)std::ceil(wMax / step);
        for (int i = i0; i <= i1; i++) {
            const float w = i * step;
            const float y = cy - (w - ov.center.y) * ov.zoom - pos.y;
            const bool major = (i % majorEvery) == 0;
            ImU32 col = w == 0.0f ? IM_COL32(160, 120, 120, 200)
                       : major ? IM_COL32(70, 70, 80, 160) : IM_COL32(48, 48, 56, 110);
            drawLineClipped(0, y, size.x, y, col);
        }
    }

    // ---- brush wireframes (Hammer draws the face polygons as outlines) ----
    for (auto& brush : m_Engine.GetBrushMap().Brushes()) {
        const bool selected = (brush.id == m_SelectedBrush);
        const ImU32 col = selected ? IM_COL32(255, 200, 60, 255) : IM_COL32(150, 190, 230, 200);
        const float thickness = selected ? 2.0f : 1.0f;
        for (auto& face : brush.faces) {
            const auto& poly = face.polygon;
            for (size_t i = 0; i < poly.size(); i++) {
                const ImVec2 a = toScreen(poly[i]);
                const ImVec2 b = toScreen(poly[(i + 1) % poly.size()]);
                dl->AddLine(a, b, col, thickness);
            }
        }
    }

    // ---- mesh entities: origin markers ------------------------------------
    m_Engine.GetScene().Each<TransformComponent>([&](Entity e, TransformComponent& tc) {
        const vec3 p = vec3(tc.worldMatrix[3]);
        const ImVec2 s = toScreen(p);
        const bool sel = (e.Handle() == m_SelectedEntity);
        const ImU32 col = sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 180);
        dl->AddLine(ImVec2(s.x - 4, s.y), ImVec2(s.x + 4, s.y), col);
        dl->AddLine(ImVec2(s.x, s.y - 4), ImVec2(s.x, s.y + 4), col);
        if (sel) {
            dl->AddText(ImVec2(s.x + 6, s.y - 14), col, e.Name().c_str());
        }
    });

    // ---- live block-drag rectangle ----------------------------------------
    if (m_BlockDragging && m_BlockDragView == view) {
        const float u0 = std::min(m_BlockDragU0, m_BlockDragU1);
        const float u1 = std::max(m_BlockDragU0, m_BlockDragU1);
        const float w0 = std::min(m_BlockDragW0, m_BlockDragW1);
        const float w1 = std::max(m_BlockDragW0, m_BlockDragW1);
        auto planeToScreen = [&](float u, float w) {
            return ImVec2(cx + (u - ov.center.x) * ov.zoom, cy - (w - ov.center.y) * ov.zoom);
        };
        const ImVec2 a = planeToScreen(u0, w0), b = planeToScreen(u1, w1);
        dl->AddRectFilled(a, b, IM_COL32(255, 180, 60, 40));
        dl->AddRect(a, b, IM_COL32(255, 180, 60, 220), 0.0f, 0, 1.5f);
    }
}

void EditorApp::DrawOrthoViewPanel(const char* title, int view) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(title);

    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 32.0f) size.x = 32.0f;
    if (size.y < 32.0f) size.y = 32.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    HandleOrthoViewInput(view, pos, size);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(26, 26, 32, 255));
    DrawOrthoViewContent(view, dl, pos, size);

    // Pane label (Hammer shows the plane + zoom in the corner).
    static const char* names[3] = { "top (x/z)", "front (x/y)", "side (z/y)" };
    char label[128];
    std::snprintf(label, sizeof(label), "%s   zoom %.0f   snap %s", names[view], m_OrthoViews[view].zoom,
                  m_GridSnap > 0.0f ? "on" : "auto");
    dl->AddRectFilled(pos, ImVec2(pos.x + ImGui::CalcTextSize(label).x + 12.0f, pos.y + 20.0f),
                      IM_COL32(0, 0, 0, 140));
    dl->AddText(ImVec2(pos.x + 6.0f, pos.y + 3.0f), IM_COL32(210, 210, 210, 255), label);

    ImGui::Dummy(size); // consume the content region so the window lays out
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// 3D-viewport brush picking (unchanged behaviour)
// ---------------------------------------------------------------------------

// Casts a ray from the mouse position (in the viewport panel's local space)
// through the current editor camera, returning world-space origin/direction.
static Ray ViewportMouseRay(const vec2& mouseScreenPos, const vec2& viewportPos, const vec2& viewportSize,
                             const RenderCamera& cam) {
    vec2 local = mouseScreenPos - viewportPos;
    float ndcX = (2.0f * local.x) / viewportSize.x - 1.0f;
    float ndcY = 1.0f - (2.0f * local.y) / viewportSize.y;

    mat4 invViewProj = glm::inverse(cam.proj * cam.view);
    vec4 nearPoint = invViewProj * vec4(ndcX, ndcY, -1.0f, 1.0f);
    vec4 farPoint = invViewProj * vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    Ray ray;
    ray.origin = vec3(nearPoint);
    ray.direction = glm::normalize(vec3(farPoint - nearPoint));
    return ray;
}

void EditorApp::HandleBrushPicking() {
    if (!m_ViewportHovered || !Input::Get().WasKeyPressed(VK_LBUTTON)) return;

    RenderCamera cam = BuildEditorCamera();
    Ray ray = ViewportMouseRay(Input::Get().MousePosition(), m_ViewportPos, m_ViewportSize, cam);

    float closestT = std::numeric_limits<float>::max();
    UUID hit{0};
    bool found = false;
    for (auto& brush : m_Engine.GetBrushMap().Brushes()) {
        AABB b = brush.Bounds();
        float t;
        if (RayAABBIntersect(ray, b, t) && t < closestT) {
            closestT = t;
            hit = brush.id;
            found = true;
        }
    }
    m_SelectedBrush = found ? hit : UUID{0};
    m_SoftwarePreviewDirty = true;
}

void EditorApp::ApplyClipTool() {
    Brush* brush = m_Engine.GetBrushMap().FindBrush(m_SelectedBrush);
    if (!brush) { FW_LOG_WARN("Clip tool: no brush selected"); return; }

    // Clip along a horizontal plane through the brush's center as a simple,
    // deterministic default (a full 3-point clip-plane widget in the
    // viewport is a natural follow-up once mouse-drag brush editing lands).
    AABB bounds = brush->Bounds();
    Plane clipPlane = Plane::FromPointNormal(bounds.Center(), vec3(0, 1, 0));

    Brush front, back;
    if (Brush::Clip(*brush, clipPlane, &front, &back)) {
        UUID oldId = brush->id;
        m_Engine.GetBrushMap().RemoveBrush(oldId);
        m_Engine.GetBrushMap().AddBrush(front);
        m_Engine.GetBrushMap().AddBrush(back);
        m_Engine.GetBrushMap().MarkAllDirty();
        m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
        m_SelectedBrush = UUID{0};
        m_SoftwarePreviewDirty = true;
        FW_LOG_INFO("Clipped brush into 2 pieces");
    } else {
        FW_LOG_WARN("Clip plane did not intersect the selected brush");
    }
}

void EditorApp::ApplyCarveTool() {
    Brush* cutter = m_Engine.GetBrushMap().FindBrush(m_SelectedBrush);
    if (!cutter) { FW_LOG_WARN("Carve tool: no cutter brush selected"); return; }

    Brush cutterCopy = *cutter;
    m_Engine.GetBrushMap().RemoveBrush(cutterCopy.id);

    std::vector<Brush> results;
    for (auto& target : m_Engine.GetBrushMap().Brushes()) {
        auto pieces = Brush::Subtract(target, cutterCopy);
        results.insert(results.end(), pieces.begin(), pieces.end());
    }
    // Replace all remaining brushes with the carved results.
    std::vector<UUID> toRemove;
    for (auto& b : m_Engine.GetBrushMap().Brushes()) toRemove.push_back(b.id);
    for (auto id : toRemove) m_Engine.GetBrushMap().RemoveBrush(id);
    for (auto& b : results) m_Engine.GetBrushMap().AddBrush(b);

    m_Engine.GetBrushMap().MarkAllDirty();
    m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
    m_SelectedBrush = UUID{0};
    m_SoftwarePreviewDirty = true;
    FW_LOG_INFO("Carved selected brush out of the level");
}

void EditorApp::UpdateHammerMode(float dt) {
    FW_UNUSED(dt);
    if (m_BrushTool == BrushTool::Select) HandleBrushPicking();

    // Delete removes the selected brush (Hammer's DEL).
    if (Input::Get().WasKeyPressed(VK_DELETE) && m_SelectedBrush != UUID{0}) {
        m_Engine.GetBrushMap().RemoveBrush(m_SelectedBrush);
        m_SelectedBrush = UUID{0};
        m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
        m_SoftwarePreviewDirty = true;
    }
}

// ---------------------------------------------------------------------------
// Hammer Tools panel: the classic vertical tool selector + block parameters
// ---------------------------------------------------------------------------

void EditorApp::DrawHammerToolPanel() {
    ImGui::Begin("Hammer Tools");
    ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.15f, 1.0f), "HAMMER MODE");
    ImGui::TextWrapped("Brush level geometry in the spirit of Valve's Hammer editor. Drag with "
                       "LMB in a 2D view to pull out a block (Block tool). Brushes are ordinary "
                       "entities: attach a Script to make one interactive.");
    ImGui::Separator();

    // ---- vertical tool list (Hammer's left-hand tool strip) ---------------
    struct ToolItem { BrushTool tool; const char* label; const char* key; };
    static const ToolItem tools[] = {
        { BrushTool::Select, "Selection",  "M" },
        { BrushTool::Block,  "Block",      "B" },
        { BrushTool::Clip,   "Clip",       "C" },
        { BrushTool::Vertex, "Vertex",     "V" },
        { BrushTool::Carve,  "Carve",      "X" },
    };
    for (const auto& t : tools) {
        const bool active = (m_BrushTool == t.tool);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.5f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.6f, 0.2f, 1.0f));
        }
        char label[64];
        std::snprintf(label, sizeof(label), "%s##tool", t.label);
        if (ImGui::Button(label, ImVec2(-1, 0))) m_BrushTool = t.tool;
        if (active) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shortcut: %s", t.key);
    }
    // Tool hotkeys (only while not typing).
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_M)) m_BrushTool = BrushTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_B)) m_BrushTool = BrushTool::Block;
        if (ImGui::IsKeyPressed(ImGuiKey_C)) m_BrushTool = BrushTool::Clip;
        if (ImGui::IsKeyPressed(ImGuiKey_V)) m_BrushTool = BrushTool::Vertex;
        if (ImGui::IsKeyPressed(ImGuiKey_X)) m_BrushTool = BrushTool::Carve;
    }

    ImGui::Separator();

    // ---- grid snap + block extrusion params ------------------------------
    static const char* snapNames[] = { "Off", "0.25", "1", "4", "16" };
    static const float snapValues[] = { 0.0f, 0.25f, 1.0f, 4.0f, 16.0f };
    int snapIdx = 2;
    for (int i = 0; i < 5; i++) if (m_GridSnap == snapValues[i]) snapIdx = i;
    ImGui::Text("Grid"); ImGui::SameLine(80);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##snap", &snapIdx, snapNames, 5)) m_GridSnap = snapValues[snapIdx];

    if (m_BrushTool == BrushTool::Block) {
        ImGui::Text("Block tool");
        ImGui::DragFloat("Base Y", &m_BlockBaseY, 0.1f);
        ImGui::DragFloat("Height (top view)", &m_BlockHeight, 0.1f, 0.05f, 500.0f);
        ImGui::DragFloat("Depth (front/side)", &m_BlockDepth, 0.1f, 0.05f, 500.0f);
        ImGui::TextWrapped("Drag a rectangle in the top, front or side view. "
                           "The top view extrudes Height from Base Y; front/side "
                           "extrude Depth around the origin.");
    } else if (m_BrushTool == BrushTool::Clip) {
        ImGui::TextWrapped("Select a brush, then Apply Clip to split it along a horizontal plane through its center.");
        ImGui::BeginDisabled(m_SelectedBrush == UUID{0});
        if (ImGui::Button("Apply Clip", ImVec2(-1, 0))) ApplyClipTool();
        ImGui::EndDisabled();
    } else if (m_BrushTool == BrushTool::Carve) {
        ImGui::TextWrapped("Select a brush to use as the cutter, then Apply Carve to subtract it from every other brush.");
        ImGui::BeginDisabled(m_SelectedBrush == UUID{0});
        if (ImGui::Button("Apply Carve", ImVec2(-1, 0))) ApplyCarveTool();
        ImGui::EndDisabled();
    } else if (m_BrushTool == BrushTool::Vertex) {
        ImGui::TextWrapped("Vertex editing: drag individual face vertices in the viewport (planned - "
                            "for now, use Clip/Carve or edit face planes directly via script).");
    } else {
        ImGui::TextWrapped("Click a brush in any view to select it. DEL deletes it.");
    }

    ImGui::Separator();
    ImGui::Text("Brushes in level: %d", (int)m_Engine.GetBrushMap().Brushes().size());
    if (m_SelectedBrush != UUID{0}) {
        Brush* b = m_Engine.GetBrushMap().FindBrush(m_SelectedBrush);
        if (b) {
            ImGui::Separator();
            ImGui::Text("Selected Brush: %llu", (unsigned long long)(u64)b->id);
            ImGui::Text("Faces: %d", (int)b->faces.size());
            ImGui::Checkbox("Is Trigger", &b->isTrigger);
            ImGui::InputText("Classname", &b->classname);
            if (ImGui::Button("Delete Brush", ImVec2(-1, 0))) {
                m_Engine.GetBrushMap().RemoveBrush(b->id);
                m_SelectedBrush = UUID{0};
                m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
                m_SoftwarePreviewDirty = true;
            }
            for (auto& face : b->faces) {
                ImGui::PushID(&face);
                ImGui::Separator();
                ImGui::InputText("Material", &face.material);
                ImGui::DragFloat2("UV Offset", &face.uvOffset.x, 0.01f);
                ImGui::DragFloat2("UV Scale", &face.uvScale.x, 0.01f);
                ImGui::DragFloat("UV Rotation", &face.uvRotationDeg, 0.5f);
                ImGui::PopID();
            }
        }
    }

    ImGui::End();
}

} // namespace fw
