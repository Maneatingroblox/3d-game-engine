#include "engine/scene/Scene.h"
#include "engine/core/Log.h"

namespace fw {

Scene::Scene(const std::string& name) : m_Name(name) {}
Scene::~Scene() = default;

Entity Scene::CreateEntity(const std::string& name) {
    return CreateEntityWithId(UUID(), name);
}

Entity Scene::CreateEntityWithId(UUID id, const std::string& name) {
    entt::entity handle = m_Registry.create();
    m_Registry.emplace<IDComponent>(handle, id);
    m_Registry.emplace<NameComponent>(handle, name);
    m_Registry.emplace<HierarchyComponent>(handle);
    m_Registry.emplace<TransformComponent>(handle);
    m_UUIDToEntity[id] = handle;
    return Entity(handle, &m_Registry);
}

void Scene::DestroyEntity(Entity entity) {
    if (!entity) return;
    // Destroy children first (copy list, DestroyEntity mutates parent's list)
    auto& hierarchy = entity.Get<HierarchyComponent>();
    auto children = hierarchy.children;
    for (auto child : children) DestroyEntity(Entity(child, &m_Registry));

    Unparent(entity);
    m_UUIDToEntity.erase(entity.Id());
    m_Registry.destroy(entity.Handle());
}

void Scene::SetParent(Entity child, Entity parent) {
    Unparent(child);
    auto& childHierarchy = child.Get<HierarchyComponent>();
    childHierarchy.parent = parent.Handle();
    parent.Get<HierarchyComponent>().children.push_back(child.Handle());
}

void Scene::Unparent(Entity child) {
    auto& h = child.Get<HierarchyComponent>();
    if (h.parent != entt::null && m_Registry.valid(h.parent)) {
        auto& parentH = m_Registry.get<HierarchyComponent>(h.parent);
        auto& kids = parentH.children;
        kids.erase(std::remove(kids.begin(), kids.end(), child.Handle()), kids.end());
    }
    h.parent = entt::null;
}

Entity Scene::FindByUUID(UUID id) {
    auto it = m_UUIDToEntity.find(id);
    if (it == m_UUIDToEntity.end() || !m_Registry.valid(it->second)) return Entity();
    return Entity(it->second, &m_Registry);
}

Entity Scene::FindByName(const std::string& name) {
    Entity result;
    auto view = m_Registry.view<NameComponent>();
    for (auto e : view) {
        if (view.get<NameComponent>(e).name == name) { result = Entity(e, &m_Registry); break; }
    }
    return result;
}

Entity Scene::PrimaryCamera() {
    Entity result;
    auto view = m_Registry.view<CameraComponent>();
    for (auto e : view) {
        if (view.get<CameraComponent>(e).isPrimary) return Entity(e, &m_Registry);
    }
    for (auto e : view) return Entity(e, &m_Registry); // fallback: first camera
    return result;
}

void Scene::UpdateTransformRecursive(entt::entity e, const mat4& parentWorld) {
    auto& tc = m_Registry.get<TransformComponent>(e);
    tc.worldMatrix = parentWorld * tc.local.ToMatrix();
    auto& h = m_Registry.get<HierarchyComponent>(e);
    for (auto child : h.children) {
        if (m_Registry.valid(child)) UpdateTransformRecursive(child, tc.worldMatrix);
    }
}

void Scene::UpdateTransforms() {
    auto view = m_Registry.view<TransformComponent, HierarchyComponent>();
    for (auto e : view) {
        auto& h = view.get<HierarchyComponent>(e);
        if (h.parent == entt::null) {
            UpdateTransformRecursive(e, mat4(1.0f));
        }
    }
}

Scene Scene::Clone() const {
    Scene copy(m_Name);
    // EnTT registries support entt::registry::assign via snapshot; for our
    // moderate entity counts (editor scenes, not MMO worlds) a straightforward
    // component-by-component copy keeps this simple and dependency-free.
    copy.m_Registry.clear();
    copy.m_UUIDToEntity.clear();

    std::unordered_map<entt::entity, entt::entity> remap;
    auto& src = const_cast<entt::registry&>(m_Registry);

    auto idView = src.view<IDComponent>();
    for (auto e : idView) {
        entt::entity ne = copy.m_Registry.create();
        remap[e] = ne;
    }
    for (auto& [oldE, newE] : remap) {
        if (auto* c = src.try_get<IDComponent>(oldE)) copy.m_Registry.emplace<IDComponent>(newE, *c);
        if (auto* c = src.try_get<NameComponent>(oldE)) copy.m_Registry.emplace<NameComponent>(newE, *c);
        if (auto* c = src.try_get<TransformComponent>(oldE)) copy.m_Registry.emplace<TransformComponent>(newE, *c);
        if (auto* c = src.try_get<MeshRendererComponent>(oldE)) copy.m_Registry.emplace<MeshRendererComponent>(newE, *c);
        if (auto* c = src.try_get<LightComponent>(oldE)) copy.m_Registry.emplace<LightComponent>(newE, *c);
        if (auto* c = src.try_get<CameraComponent>(oldE)) copy.m_Registry.emplace<CameraComponent>(newE, *c);
        if (auto* c = src.try_get<SkyLightComponent>(oldE)) copy.m_Registry.emplace<SkyLightComponent>(newE, *c);
        if (auto* c = src.try_get<RigidBodyComponent>(oldE)) copy.m_Registry.emplace<RigidBodyComponent>(newE, *c);
        if (auto* c = src.try_get<ColliderComponent>(oldE)) copy.m_Registry.emplace<ColliderComponent>(newE, *c);
        if (auto* c = src.try_get<CharacterControllerComponent>(oldE)) copy.m_Registry.emplace<CharacterControllerComponent>(newE, *c);
        if (auto* c = src.try_get<AudioSourceComponent>(oldE)) copy.m_Registry.emplace<AudioSourceComponent>(newE, *c);
        if (auto* c = src.try_get<AudioListenerComponent>(oldE)) copy.m_Registry.emplace<AudioListenerComponent>(newE, *c);
        if (auto* c = src.try_get<ScriptComponent>(oldE)) {
            ScriptComponent sc = *c;
            sc.runtimeInstance = nullptr;
            copy.m_Registry.emplace<ScriptComponent>(newE, sc);
        }
        if (auto* c = src.try_get<BrushComponent>(oldE)) copy.m_Registry.emplace<BrushComponent>(newE, *c);

        HierarchyComponent newH;
        if (auto* h = src.try_get<HierarchyComponent>(oldE)) {
            newH.parent = (h->parent != entt::null && remap.count(h->parent)) ? remap[h->parent] : entt::null;
            for (auto child : h->children) if (remap.count(child)) newH.children.push_back(remap[child]);
        }
        copy.m_Registry.emplace<HierarchyComponent>(newE, newH);

        copy.m_UUIDToEntity[copy.m_Registry.get<IDComponent>(newE).id] = newE;
    }
    return copy;
}

} // namespace fw
