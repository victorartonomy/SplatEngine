#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include <glad/glad.h>
#include <vector>

#include "Entity.h"
#include "Light.h"

class LightManager {
public:
    LightManager()  = default;
    ~LightManager() = default;

    LightManager(const LightManager&)            = delete;
    LightManager& operator=(const LightManager&) = delete;

    void update(const std::vector<Entity>& entities);
    void bind(GLuint bindingPoint) const;
    void shutdown();

    int          getLightCount()   const { return m_lightCount; }
    ShadingModel getShadingModel() const { return m_shadingModel; }
    void         setShadingModel(ShadingModel model) { m_shadingModel = model; }

private:
    GLuint       m_ssbo = 0;
    int          m_lightCount = 0;
    ShadingModel m_shadingModel = ShadingModel::BlinnPhong;
};

#endif // LIGHT_MANAGER_H
