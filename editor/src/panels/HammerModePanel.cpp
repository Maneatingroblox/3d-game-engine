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
}

void EditorApp::DrawHammerToolPanel() {
    ImGui::Begin("Hammer Tools");
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "HAMMER MODE");
    ImGui::TextWrapped("Brush-based level geometry, in the spirit of Valve's Hammer editor. "
                        "Brushes are still ordinary entities: attach a Script component to any "
                        "brush to make it interactive.");
    ImGui::Separator();

    const char* toolNames[] = { "Select", "Block", "Clip", "Vertex", "Carve" };
    int tool = (int)m_BrushTool;
    if (ImGui::Combo("Tool", &tool, toolNames, 5)) m_BrushTool = (BrushTool)tool;

    ImGui::Separator();

    static vec3 blockMins(-1, 0, -1), blockMaxs(1, 2, 1);
    if (m_BrushTool == BrushTool::Block) {
        ImGui::Text("New Block");
        ImGui::DragFloat3("Mins", &blockMins.x, 0.1f);
        ImGui::DragFloat3("Maxs", &blockMaxs.x, 0.1f);
        if (ImGui::Button("Create Block Brush", ImVec2(-1, 0))) {
            Brush b = Brush::CreateBox(blockMins, blockMaxs);
            b.Rebuild();
            m_Engine.GetBrushMap().AddBrush(b);
            m_Engine.GetBrushMap().MarkAllDirty();
            m_Engine.GetBrushMap().CompileToScene(m_Engine.GetScene(), Paths::Resolve("assets/generated"));
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
