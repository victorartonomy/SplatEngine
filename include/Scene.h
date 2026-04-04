#ifndef SCENE_H
#define SCENE_H

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Entity.h"

class AssetManager;

class Scene {
public:
    Entity& createEntity(const std::string& name);
    void    removeEntity(uint32_t id);

    std::vector<Entity>& getEntities() { return m_entities; }

    void recalculateSceneBounds(const AssetManager& am);

    glm::vec3 getSceneCenter() const { return m_sceneCenter; }
    glm::vec3 getSceneSize()   const { return m_sceneSize;   }
    glm::vec3 getSceneMin()    const { return m_sceneMin;    }
    glm::vec3 getSceneMax()    const { return m_sceneMax;    }

    bool hasVisibleMeshes() const;

private:
    std::vector<Entity> m_entities;
    uint32_t            m_nextEntityId = 1;

    glm::vec3 m_sceneCenter{0.0f};
    glm::vec3 m_sceneSize{0.0f};
    glm::vec3 m_sceneMin{0.0f};
    glm::vec3 m_sceneMax{0.0f};
};

#endif // SCENE_H
