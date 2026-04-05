#include "Scene.h"
#include "AssetManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

// ============================================
// Transform
// ============================================

// Build a 4×4 model matrix from this transform's position, rotation, and scale.
//
// Composition order: T * Rx * Ry * Rz * S
//   - Translation is applied last in column-major convention (outermost transform)
//   - Rotations are applied in XYZ Euler order: X first, then Y, then Z
//   - Scale is applied first (innermost transform), so it does not affect the
//     rotation or translation axes
//
// This order means scale → rotate → translate in object space, which matches
// the standard "TRS" convention used by most 3D editors.
glm::mat4 Transform::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale);
    return model;
}

// ============================================
// Scene
// ============================================

// Create a new entity with a unique auto-incrementing ID and add it to the entity list.
// Returns a reference to the entity in the flat vector — callers should not store this
// reference across subsequent createEntity() calls (vector reallocation may invalidate it).
Entity& Scene::createEntity(const std::string& name) {
    Entity e;
    e.id   = m_nextEntityId++;  // ID is unique and never reused across the scene's lifetime
    e.name = name;
    m_entities.push_back(std::move(e));
    return m_entities.back();
}

// Remove the entity with the given ID from the scene.
// Uses the erase-remove idiom for efficient in-place removal from the flat vector.
// Any references or pointers to entities in the vector are invalidated after this call.
void Scene::removeEntity(uint32_t id) {
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [id](const Entity& e) { return e.id == id; }),
        m_entities.end());
}

// Returns true if at least one visible entity has a valid mesh asset attached.
// Used by Renderer::render() to skip the dispatch when there is nothing to draw.
bool Scene::hasVisibleMeshes() const {
    for (const auto& e : m_entities) {
        if (e.visible && e.meshAsset.isValid())
            return true;
    }
    return false;
}

// Recompute the world-space axis-aligned bounding box of all visible mesh entities.
// Updates m_sceneMin, m_sceneMax, m_sceneCenter, and m_sceneSize.
//
// The AABB is computed by transforming all 8 corners of each mesh's local-space AABB
// through the entity's model matrix. This is necessary because rotation breaks the
// axis alignment: applying the model matrix only to the min/max corners would yield
// an incorrect (too-small) AABB for rotated objects.
//
// If no visible meshes exist, all bounds are zeroed so callers don't get stale values.
void Scene::recalculateSceneBounds(const AssetManager& am) {
    // Start with inverted extremes so the first real point always wins the min/max test
    glm::vec3 sceneMin( 1e10f);
    glm::vec3 sceneMax(-1e10f);
    bool anyVisible = false;

    for (const auto& entity : m_entities) {
        if (!entity.visible || !entity.meshAsset.isValid())
            continue;
        const MeshAsset* asset = am.get(entity.meshAsset);
        if (!asset)
            continue;

        anyVisible = true;
        glm::mat4 model = entity.transform.getModelMatrix();

        // Enumerate all 8 corners of the local-space AABB (2^3 = 8 combinations of lo/hi per axis).
        // Transforming all corners handles the case where rotation maps one AABB corner
        // to a world-space extreme that neither boundsMin nor boundsMax would capture alone.
        glm::vec3 lo = asset->boundsMin;
        glm::vec3 hi = asset->boundsMax;
        glm::vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
            {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
            {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
        };
        for (const auto& c : corners) {
            // Transform local corner to world space; w=1 for a position vector
            glm::vec3 world = glm::vec3(model * glm::vec4(c, 1.0f));
            sceneMin = glm::min(sceneMin, world);
            sceneMax = glm::max(sceneMax, world);
        }
    }

    // If nothing is visible, reset all bounds to avoid stale values from a previous frame
    if (!anyVisible) {
        m_sceneMin    = glm::vec3(0.0f);
        m_sceneMax    = glm::vec3(0.0f);
        m_sceneCenter = glm::vec3(0.0f);
        m_sceneSize   = glm::vec3(0.0f);
        return;
    }

    m_sceneMin    = sceneMin;
    m_sceneMax    = sceneMax;
    m_sceneCenter = (sceneMin + sceneMax) * 0.5f;  // Midpoint used as orbit pivot and auto-focus target
    m_sceneSize   = sceneMax - sceneMin;             // Diagonal extent used to scale camera speed
}
