#ifndef MATERIAL_MANAGER_H
#define MATERIAL_MANAGER_H

#include <glad/glad.h>
#include <vector>
#include <cstdint>

#include "Mesh.h"  // GPUMaterial

// MaterialManager — owns the GPU material SSBO (binding 8 in pass4_rasterize_tiled.comp).
//
// Mirrors the LightManager pattern:
//   - CPU array of GPUMaterial structs is the authoritative state.
//   - upload() pushes the full array to a GL_DYNAMIC_DRAW SSBO.
//   - bind(n) calls glBindBufferBase so the shader can read the array.
//   - shutdown() releases the GL buffer.
//
// Slot 0 is always the default white/neutral material inserted in the constructor.
// Faces parsed from COFF files are assigned materialID=0, so existing meshes render
// identically (face.color × albedo(1,1,1) = face.color — no visual regression).
class MaterialManager {
public:
    // Inserts the default material at slot 0 on construction.
    MaterialManager();
    ~MaterialManager() = default;

    MaterialManager(const MaterialManager&)            = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;

    // Append a new material and return its slot index (to be stored as Face::materialID).
    uint32_t addMaterial(const GPUMaterial& mat);

    // Overwrite an existing material by index. No-op if id is out of range.
    void setMaterial(uint32_t id, const GPUMaterial& mat);

    // Read a material by index. Falls back to slot 0 if id is out of range.
    const GPUMaterial& getMaterial(uint32_t id) const;

    // Number of material slots (always >= 1 due to the default inserted at construction).
    int getMaterialCount() const { return static_cast<int>(m_materials.size()); }

    // Push the full CPU array to the SSBO. Creates the buffer on first call.
    // Must be called on the main GL thread. Uses GL_DYNAMIC_DRAW because materials
    // can be edited at runtime via the Materials ImGui panel.
    void upload();

    // Bind the SSBO to the given shader storage binding point.
    void bind(GLuint bindingPoint) const;

    // Release the GL buffer. Must be called before the OpenGL context is destroyed.
    void shutdown();

private:
    GLuint                   m_ssbo = 0;   // GL buffer handle (0 = not yet created)
    std::vector<GPUMaterial> m_materials;  // CPU-side material array
};

#endif // MATERIAL_MANAGER_H
