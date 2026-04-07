#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include <glad/glad.h>
#include <vector>
#include <glm/glm.hpp>

#include "Entity.h"
#include "Light.h"

// LightManager — builds and maintains the GPU light SSBO (binding 7 in pass4).
//
// Each frame, update() scans the entity list for entities with a light component,
// converts them to GPULight structs, and uploads them to an SSBO. If no light
// entities exist, a hard-coded default directional light is used as fallback so
// the scene is never rendered completely dark.
//
// The active shading model (Blinn-Phong or PBR) is also stored here and forwarded
// to the rasterizer shader as the "shadingModel" uniform.
class LightManager {
public:
    LightManager()  = default;
    ~LightManager() = default;

    // Non-copyable — owns a GL buffer object
    LightManager(const LightManager&)            = delete;
    LightManager& operator=(const LightManager&) = delete;

    // Rebuild the GPU light SSBO from the current entity list.
    // Creates the GL buffer on first call; reallocates if the light count changes.
    // Must be called on the main (GL) thread.
    void update(const std::vector<Entity>& entities);

    // Bind the light SSBO to the given shader storage binding point.
    // Call this before dispatching pass4 so the shader can read lights[].
    void bind(GLuint bindingPoint) const;

    // Release the GL buffer. Must be called before the OpenGL context is destroyed.
    void shutdown();

    // Returns the number of lights currently in the SSBO (at least 1 — the fallback).
    int getLightCount() const { return m_lightCount; }

    // Active shading model forwarded to the rasterizer shader as a uniform.
    ShadingModel getShadingModel() const               { return m_shadingModel; }
    void         setShadingModel(ShadingModel model)   { m_shadingModel = model; }

    // Returns the light-view-projection matrix of the first shadow-casting light
    // found during the most recent update(), or identity if none exist. Used by
    // the Renderer's shadow pass and by pass4 to sample the shadow map.
    const glm::mat4& getShadowViewProjection() const { return m_shadowViewProjection; }

    // Index (into lights[]) of the shadow-casting light, or -1 if none.
    // pass4 uses this to know which light's contribution should be modulated by the shadow factor.
    int getShadowLightIndex() const { return m_shadowLightIndex; }

private:
    GLuint       m_ssbo        = 0;                       // GL SSBO handle (0 = not yet created)
    int          m_lightCount  = 0;                       // Number of lights in the current upload
    ShadingModel m_shadingModel = ShadingModel::BlinnPhong; // Current shading model selection

    // Cached during update(): the first shadow-casting light's VP matrix and index.
    glm::mat4 m_shadowViewProjection = glm::mat4(1.0f);
    int       m_shadowLightIndex     = -1;
};

#endif // LIGHT_MANAGER_H
