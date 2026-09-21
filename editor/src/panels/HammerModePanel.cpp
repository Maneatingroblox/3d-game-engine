// Hammer mode: Valve Hammer Editor-style brush authoring layered on top of
// the same ECS scene. New brushes are created as axis-aligned blocks that
// the user drags out in the viewport (like Hammer's Block tool), then
// edited with Clip (split a brush along a 3-point plane) and Carve
// (boolean-subtract one brush's volume from others). Every brush is
// immediately compiled into a MeshRendererComponent + ColliderComponent
// entity via BrushMap::CompileToScene, so Play mode, physics and scripts
// see brush geometry exactly like any hand-placed mesh - Hammer mode is
// purely an authoring convenience, not a separate runtime.
#include "editor/EditorApp.h"
#include "engine/core/Log.h"
#include "engine/core/Paths.h"
#include "engine/platform/Input.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fw {

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
    // Only pick on a fresh left-click inside the 3D viewport, and never while
    // ImGui owns the mouse (a click on the tool palette, a menu or the gizmo
    // must not also reach through and reselect geometry behind it).
    if (!m_ViewportHovered) return;
    if (ImGui::GetIO().WantCaptureMouse && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) return;

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
}

void EditorApp::DrawHammerToolPanel() {
    ImGui::Begin("Hammer Tools");
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "HAMMER MODE");
    ImGui::TextWrapped("Brush-based level geometry, in the spirit of Valve's Hammer editor. "
                        "Brushes are still ordinary entities: attach a Script component to any "
                        "brush to make it interactive.");
    ImGui::Separator();

    // ---- tool palette: big toggle buttons, like Hammer's left-hand bar ----
    const char* toolNames[] = { "Select", "Block", "Clip", "Vertex", "Carve" };
    const char* toolHelp[] = {
        "Click geometry in the 3D or 2D views to select it",
        "Create a new axis-aligned block brush",
        "Split the selected brush along a plane",
        "Inspect/adjust the selected brush's vertices",
        "Boolean-subtract the selected brush from the others",
    };
    ImGui::TextDisabled("Tools");
    for (int i = 0; i < 5; i++) {
        const bool active = ((int)m_BrushTool == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.5f, 0.1f, 1.0f));
        if (ImGui::Button(toolNames[i], ImVec2(-1, 0))) m_BrushTool = (BrushTool)i;
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toolHelp[i]);
    }

    ImGui::Separator();

    // ---- grid snap, the other thing every Hammer user reaches for --------
    ImGui::TextDisabled("Grid");
    ImGui::Checkbox("Snap to grid", &m_BrushSnapEnabled);
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##gridsize", &m_BrushGridSize, 0.05f, kMinGridSize, kMaxGridSize, "grid %.3f");
    if (ImGui::SmallButton("[ smaller")) StepGridSize(-1);
    ImGui::SameLine();
    if (ImGui::SmallButton("bigger ]")) StepGridSize(+1);
    ImGui::SameLine();
    ImGui::TextDisabled("%.3f", m_BrushGridSize);

    ImGui::Separator();
    ImGui::TextDisabled("Navigation");
    ImGui::BulletText("Z - mouselook in the 3D view");
    ImGui::BulletText("WASD / QE - fly, Shift to sprint");
    ImGui::BulletText("RMB drag - look, MMB - pan");
    ImGui::BulletText("[ / ] - grid size");
    ImGui::BulletText("Click - select (3D or 2D views)");

    ImGui::Separator();

    static vec3 blockMins(-1, 0, -1), blockMaxs(1, 2, 1);
    if (m_BrushTool == BrushTool::Block) {
        ImGui::Text("New Block");
        ImGui::DragFloat3("Mins", &blockMins.x, 0.1f);
        ImGui::DragFloat3("Maxs", &blockMaxs.x, 0.1f);

        auto snap = [this](vec3 v) {
            if (!m_BrushSnapEnabled || m_BrushGridSize <= 0.0f) return v;
            for (int i = 0; i < 3; i++) v[i] = std::round(v[i] / m_BrushGridSize) * m_BrushGridSize;
            return v;
        };

        if (ImGui::Button("Create Block Brush", ImVec2(-1, 0))) {
            vec3 mins = snap(blockMins), maxs = snap(blockMaxs);
            // A zero-thickness box makes a degenerate brush; keep at least one
            // grid cell on every axis.
            for (int i = 0; i < 3; i++)
                if (maxs[i] - mins[i] < 1e-4f) maxs[i] = mins[i] + std::max(m_BrushGridSize, 0.25f);

            Brush b = Brush::CreateBox(mins, maxs);
            b.Rebuild();
            if (!b.IsValid()) {
                FW_LOG_WARN("Block tool: those bounds do not enclose a volume");
            } else {
                const UUID id = m_Engine.GetBrushMap().AddBrush(b).id;
                m_Engine.GetBrushMap().MarkAllDirty();
                m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
                m_SelectedBrush = id;
                m_SoftwarePreviewDirty = true;
                FW_LOG_INFO("Created block brush (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)",
                            mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
            }
        }
        if (ImGui::Button("Create Block In Front Of Camera", ImVec2(-1, 0))) {
            const vec3 c = snap(m_CamPos + YawPitchForward(m_CamYaw, m_CamPitch) * 6.0f);
            const vec3 half(1.0f, 1.0f, 1.0f);
            Brush b = Brush::CreateBox(c - half, c + half);
            b.Rebuild();
            const UUID id = m_Engine.GetBrushMap().AddBrush(b).id;
            m_Engine.GetBrushMap().MarkAllDirty();
            m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
            m_SelectedBrush = id;
            m_SoftwarePreviewDirty = true;
        }
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
        ImGui::TextWrapped("Click a brush in the viewport to select it.");
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
