#ifndef MESH_H
#define MESH_H

#include <glm/glm.hpp>
#include <vector>

// Vertex — one entry in the vertex SSBO uploaded to the GPU.
// Layout must exactly match the GLSL struct in all compute shaders (std430 rules):
//   - vec3 in std430 has 16-byte alignment, so a float pad follows each vec3.
//   - Total struct size = 32 bytes (2 × 16-byte aligned slots).
struct Vertex {
    glm::vec3 position; // World-space position of this vertex (transformed at submitScene time)
    float _pad0;        // Padding: rounds position+float to 16 bytes as required by std430
    glm::vec3 normal;   // Smooth per-vertex normal (computed by COFFParser, then transformed by normal matrix)
    float _pad1;        // Padding: keeps struct size a multiple of 16 bytes
};

// Face — one triangle entry in the face SSBO uploaded to the GPU.
// Layout must exactly match the GLSL struct in all compute shaders (std430 rules):
//   - uvec3 in std430 has 16-byte alignment, so an unsigned int pad follows.
//   - Total struct size = 32 bytes.
struct Face {
    glm::uvec3   indices;        // bytes  0–11: vertex indices (v0, v1, v2)
    unsigned int _pad0;          // bytes 12–15: padding to align to 16-byte boundary
    glm::vec3    color;          // bytes 16–27: per-face RGB tint (from COFF file, or default)
    unsigned int materialID;     // bytes 28–31: index into MaterialManager's GPU SSBO
};
static_assert(sizeof(Face) == 32, "Face must be 32 bytes for std430");

// GPUMaterial — std430-compatible 32-byte material parameter block uploaded to SSBO binding 8.
// One entry per material slot. Face::materialID indexes into this array.
// Slot 0 is always a default white/neutral material:
//   albedo = (1,1,1)  →  face.color × (1,1,1) = face.color  (no visual change for existing meshes)
//   metallic = 0, roughness = 0.5, emissive = 0
struct GPUMaterial {
    glm::vec3 albedo;    // bytes  0–11: base color multiplied with face.color in the shader
    float     metallic;  // bytes 12–15: 0=dielectric, 1=full metal (PBR Cook-Torrance only)
    float     roughness; // bytes 16–19: 0=mirror smooth, 1=fully diffuse (PBR only)
    float     emissive;  // bytes 20–23: emissive intensity; final += albedo × emissive
    float     _pad0;     // bytes 24–27: explicit padding to reach 32-byte struct size
    float     _pad1;     // bytes 28–31: explicit padding to reach 32-byte struct size
};
static_assert(sizeof(GPUMaterial) == 32, "GPUMaterial must be 32 bytes for std430");

// Mesh — CPU-side container for geometry data before it is uploaded to the GPU.
// Passed to GPUBuffer::uploadMesh() to create the vertex and face SSBOs.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;

    size_t getVertexCount()      const { return vertices.size(); }
    size_t getFaceCount()        const { return faces.size(); }
    // Total byte sizes used when calling glBufferData
    size_t getVertexBufferSize() const { return vertices.size() * sizeof(Vertex); }
    size_t getFaceBufferSize()   const { return faces.size() * sizeof(Face); }
};

#endif // MESH_H
