#include "LightManager.h"

// Rebuild the GPU light SSBO from the current state of all scene entities.
// Called once per frame by Renderer::render() before the pass4 dispatch.
//
// Strategy:
//   1. Iterate the entity list and convert every visible entity with a Light
//      component into a GPULight struct using toGPULight() (see Light.h).
//   2. If no lights are found, insert a hard-coded default directional light
//      so the scene is never rendered completely unlit regardless of scene content.
//   3. Upload the GPULight array to a GL SSBO. The SSBO is created on first use
//      and reallocated via GL_DYNAMIC_DRAW whenever the light count changes —
//      this is acceptable because light arrays are small (typically < 16 lights).
//
// The SSBO is bound to binding point 7 in pass4_rasterize_tiled.comp.
// This binding was chosen because bindings 0–6 are already used by other buffers
// in that shader (vertices, faces, tiles, prefix sums, depth image, output image).
void LightManager::update(const std::vector<Entity>& entities) {
    std::vector<GPULight> gpuLights;

    // Collect all visible entities that have a light component
    for (const auto& e : entities) {
        if (!e.light.has_value() || !e.visible)
            continue;
        // toGPULight() converts CPU Light + Transform into a std430-compatible GPULight,
        // pre-computing cone cosines and building the direction from Euler angles
        gpuLights.push_back(toGPULight(*e.light, e.transform));
    }

    // Fallback: if the scene has no lights, provide a single default directional light.
    // Direction (-0.4, -1, -0.6) approximates a sun at roughly 3 o'clock and 60° elevation,
    // giving visible shadows on most mesh orientations without any scene setup.
    if (gpuLights.empty()) {
        GPULight defaultLight{};
        defaultLight.position      = glm::vec3(0.0f);       // Unused for directional lights
        defaultLight.type          = 0.0f;                  // 0 = LightType::Directional (stored as float)
        defaultLight.direction     = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
        defaultLight.intensity     = 1.0f;
        defaultLight.color         = glm::vec3(1.0f);       // White light
        defaultLight.range         = 0.0f;                  // Unused for directional lights
        defaultLight.innerCosAngle = 1.0f;                  // Unused for directional lights (cos(0°) = 1)
        defaultLight.outerCosAngle = 1.0f;
        defaultLight.castsShadow   = 1.0f;                  // Default light casts shadows so the scene has occlusion
        defaultLight._pad0         = 0.0f;

        // Build the same light-view-projection that toGPULight() would produce for a real
        // directional light, so the shadow pass can use this fallback identically.
        {
            glm::vec3 lightPos = -defaultLight.direction * 100.0f;
            glm::vec3 up       = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(defaultLight.direction, up)) > 0.99f)
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), up);
            glm::mat4 lightProj = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 200.0f);
            defaultLight.lightViewProjection = lightProj * lightView;
        }
        gpuLights.push_back(defaultLight);
    }

    m_lightCount = static_cast<int>(gpuLights.size());

    // Cache the first shadow-casting light's VP matrix + index for the renderer.
    // The shadow pass needs the matrix as a single uniform; pass4 needs the index so
    // it can apply the shadow factor only to that one light's contribution.
    m_shadowLightIndex     = -1;
    m_shadowViewProjection = glm::mat4(1.0f);
    for (int i = 0; i < m_lightCount; ++i) {
        if (gpuLights[i].castsShadow > 0.5f) {
            m_shadowLightIndex     = i;
            m_shadowViewProjection = gpuLights[i].lightViewProjection;
            break;
        }
    }

    // Create the SSBO on first call (lazy allocation avoids needing a GL context at construction)
    if (m_ssbo == 0)
        glGenBuffers(1, &m_ssbo);

    // Re-upload the entire array each frame. GL_DYNAMIC_DRAW signals the driver that
    // the buffer content changes frequently, allowing it to use a suitable memory pool.
    // glBufferData is used instead of glBufferSubData to handle size changes (e.g.,
    // when the user adds or removes a light entity) without manual reallocation.
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 gpuLights.size() * sizeof(GPULight),
                 gpuLights.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Bind the light SSBO to the specified binding point so the pass4 shader can read it.
// Must be called after update() and before glDispatchCompute for the rasterize pass.
// Guard against m_ssbo == 0 in case update() was never called.
void LightManager::bind(GLuint bindingPoint) const {
    if (m_ssbo != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_ssbo);
}

// Release the SSBO and reset internal state.
// Must be called before the OpenGL context is destroyed (e.g., in Application::shutdown()).
void LightManager::shutdown() {
    if (m_ssbo != 0) {
        glDeleteBuffers(1, &m_ssbo);
        m_ssbo = 0;
    }
    m_lightCount = 0;
}
