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
    glm::uvec3 indices;      // Indices into the vertex buffer (v0, v1, v2)
    unsigned int _pad0;      // Padding: rounds uvec3 to 16-byte boundary
    glm::vec3 color;         // Per-face RGB albedo (parsed from COFF file, or default grey)
    float padding;           // Padding: keeps struct size a multiple of 16 bytes
};

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
