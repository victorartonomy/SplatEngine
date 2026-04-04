#include "LightManager.h"

void LightManager::update(const std::vector<Entity>& entities) {
    std::vector<GPULight> gpuLights;

    for (const auto& e : entities) {
        if (!e.light.has_value() || !e.visible)
            continue;
        gpuLights.push_back(toGPULight(*e.light, e.transform));
    }

    // Default directional light if none exist
    if (gpuLights.empty()) {
        GPULight defaultLight{};
        defaultLight.position  = glm::vec3(0.0f);
        defaultLight.type      = 0.0f; // Directional
        defaultLight.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
        defaultLight.intensity = 1.0f;
        defaultLight.color     = glm::vec3(1.0f);
        defaultLight.range     = 0.0f;
        defaultLight.innerCosAngle = 1.0f;
        defaultLight.outerCosAngle = 1.0f;
        defaultLight._pad0 = 0.0f;
        defaultLight._pad1 = 0.0f;
        gpuLights.push_back(defaultLight);
    }

    m_lightCount = static_cast<int>(gpuLights.size());

    if (m_ssbo == 0)
        glGenBuffers(1, &m_ssbo);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 gpuLights.size() * sizeof(GPULight),
                 gpuLights.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void LightManager::bind(GLuint bindingPoint) const {
    if (m_ssbo != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_ssbo);
}

void LightManager::shutdown() {
    if (m_ssbo != 0) {
        glDeleteBuffers(1, &m_ssbo);
        m_ssbo = 0;
    }
    m_lightCount = 0;
}
