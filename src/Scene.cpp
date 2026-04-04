#include "Scene.h"
#include "AssetManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

// ============================================
// Transform
// ============================================

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

Entity& Scene::createEntity(const std::string& name) {
    Entity e;
    e.id   = m_nextEntityId++;
    e.name = name;
    m_entities.push_back(std::move(e));
    return m_entities.back();
}

void Scene::removeEntity(uint32_t id) {
    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(),
                       [id](const Entity& e) { return e.id == id; }),
        m_entities.end());
}

bool Scene::hasVisibleMeshes() const {
    for (const auto& e : m_entities) {
        if (e.visible && e.meshAsset.isValid())
            return true;
    }
    return false;
}

void Scene::recalculateSceneBounds(const AssetManager& am) {
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

        // Transform all 8 AABB corners to handle rotation correctly
        glm::vec3 lo = asset->boundsMin;
        glm::vec3 hi = asset->boundsMax;
        glm::vec3 corners[8] = {
            {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
            {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
            {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
            {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
        };
        for (const auto& c : corners) {
            glm::vec3 world = glm::vec3(model * glm::vec4(c, 1.0f));
            sceneMin = glm::min(sceneMin, world);
            sceneMax = glm::max(sceneMax, world);
        }
    }

    if (!anyVisible) {
        m_sceneMin    = glm::vec3(0.0f);
        m_sceneMax    = glm::vec3(0.0f);
        m_sceneCenter = glm::vec3(0.0f);
        m_sceneSize   = glm::vec3(0.0f);
        return;
    }

    m_sceneMin    = sceneMin;
    m_sceneMax    = sceneMax;
    m_sceneCenter = (sceneMin + sceneMax) * 0.5f;
    m_sceneSize   = sceneMax - sceneMin;
}
