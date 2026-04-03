#ifndef MESH_H
#define MESH_H

#include <glm/glm.hpp>
#include <vector>

struct Vertex {
    glm::vec3 position;
    float _pad0;           // std430: vec3 has 16-byte alignment
    glm::vec3 padding;
    float _pad1;           // struct size must be multiple of 16
};

struct Face {
    glm::uvec3 indices;
    unsigned int _pad0;    // std430: uvec3 has 16-byte alignment
    glm::vec3 color;
    float padding;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    
    size_t getVertexCount() const { return vertices.size(); }
    size_t getFaceCount() const { return faces.size(); }
    size_t getVertexBufferSize() const { return vertices.size() * sizeof(Vertex); }
    size_t getFaceBufferSize() const { return faces.size() * sizeof(Face); }
};

#endif // MESH_H
