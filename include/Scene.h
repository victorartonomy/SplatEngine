#ifndef SCENE_H
#define SCENE_H

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Entity.h"
#include "Mesh.h"
#include "GPUBuffer.h"

struct MeshAsset {
    std::string filePath;
    Mesh        mesh;
    GPUBuffer   gpuBuffer;        // movable — safe in std::vector
    glm::vec3   boundsMin{0.0f};
    glm::vec3   boundsMax{0.0f};
    glm::vec3   boundsCenter{0.0f};
    glm::vec3   boundsSize{0.0f};
};

class Scene {
public:
    // Load a mesh from disk and cache it. Returns asset index, or -1 on failure.
    // If the same filePath was loaded before, returns the existing index (deduplication).
    int loadMeshAsset(const std::string& filePath);

    // Create a new entity and return a reference to it.
    Entity& createEntity(const std::string& name);

    // Remove the entity with the given id.
    void removeEntity(uint32_t id);

    std::vector<Entity>&    getEntities()    { return m_entities;    }
    std::vector<MeshAsset>& getMeshAssets()  { return m_meshAssets;  }
    const MeshAsset*        getMeshAsset(int index) const;

    // Recalculate scene-wide bounding box from all visible entities.
    void recalculateSceneBounds();

    glm::vec3 getSceneCenter() const { return m_sceneCenter; }
    glm::vec3 getSceneSize()   const { return m_sceneSize;   }
    glm::vec3 getSceneMin()    const { return m_sceneMin;    }
    glm::vec3 getSceneMax()    const { return m_sceneMax;    }

    bool hasVisibleMeshes() const;

private:
    std::vector<MeshAsset> m_meshAssets;
    std::vector<Entity>    m_entities;
    uint32_t               m_nextEntityId = 1;

    glm::vec3 m_sceneCenter{0.0f};
    glm::vec3 m_sceneSize{0.0f};
    glm::vec3 m_sceneMin{0.0f};
    glm::vec3 m_sceneMax{0.0f};
};

#endif // SCENE_H
