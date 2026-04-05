#include "MaterialManager.h"

// Constructor: insert the default material at slot 0.
// Any face with materialID=0 gets: final_albedo = face.color × (1,1,1) = face.color.
// This preserves the existing per-face color behavior for all COFF meshes loaded before
// additional materials are created.
MaterialManager::MaterialManager() {
    GPUMaterial def{};
    def.albedo    = glm::vec3(1.0f);  // White tint — face.color passes through unchanged
    def.metallic  = 0.0f;             // Dielectric by default
    def.roughness = 0.5f;             // Mid-range roughness matches the old hardcoded value
    def.emissive  = 0.0f;
    def._pad0     = 0.0f;
    def._pad1     = 0.0f;
    m_materials.push_back(def);
}

// Append a new material and return its slot index.
// The caller should store the returned index as Face::materialID for any faces
// that should use this material.
uint32_t MaterialManager::addMaterial(const GPUMaterial& mat) {
    uint32_t id = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(mat);
    return id;
}

// Overwrite a material in-place by index.
// Call upload() afterward to push the change to the GPU.
void MaterialManager::setMaterial(uint32_t id, const GPUMaterial& mat) {
    if (id < static_cast<uint32_t>(m_materials.size()))
        m_materials[id] = mat;
}

// Return a material by index. Falls back to slot 0 if id is out of range
// so callers never have to bounds-check before reading.
const GPUMaterial& MaterialManager::getMaterial(uint32_t id) const {
    if (id < static_cast<uint32_t>(m_materials.size()))
        return m_materials[id];
    return m_materials[0];  // Always valid — constructor guarantees slot 0 exists
}

// Push the full CPU material array to the GPU SSBO.
// glGenBuffers is deferred to the first call so the constructor is context-independent.
// GL_DYNAMIC_DRAW signals the driver that the buffer will be updated frequently
// (at least once per material edit from the Materials panel).
void MaterialManager::upload() {
    if (m_ssbo == 0)
        glGenBuffers(1, &m_ssbo);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(m_materials.size() * sizeof(GPUMaterial)),
                 m_materials.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Bind the material SSBO to the given binding point.
// Called once per frame in Renderer::render() before dispatching pass4.
void MaterialManager::bind(GLuint bindingPoint) const {
    if (m_ssbo != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_ssbo);
}

// Release the GL buffer. Must be called before the OpenGL context is destroyed.
void MaterialManager::shutdown() {
    if (m_ssbo != 0) {
        glDeleteBuffers(1, &m_ssbo);
        m_ssbo = 0;
    }
    m_materials.clear();
}
