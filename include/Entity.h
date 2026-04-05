#ifndef ENTITY_H
#define ENTITY_H

#include <string>
#include <cstdint>
#include <optional>
#include "AssetHandle.h"
#include "Light.h"
#include "Transform.h"

// Entity — a scene object that can be a mesh, a light, or both.
//
// Entities are stored flat in Scene::m_entities. They are not polymorphic;
// optional components (meshAsset, light) determine what roles the entity plays:
//   - meshAsset.isValid()  → rendered as geometry by Renderer::submitScene()
//   - light.has_value()    → contributes to the GPU light buffer via LightManager
//
// Entities without either component (e.g., empty transforms) are valid but have no effect.
struct Entity {
    uint32_t    id      = 0;    // Unique ID assigned by Scene::createEntity(); never reused
    std::string name;           // Display name shown in the editor's Scene Hierarchy panel
    Transform   transform;      // World-space position, rotation (Euler °), and scale
    AssetHandle meshAsset;      // Handle into AssetManager; invalid (id=0) if no mesh attached
    bool        visible = true; // When false, excluded from submitScene() and LightManager::update()
    std::optional<Light> light; // Light component — absent for pure mesh entities
};

#endif // ENTITY_H
