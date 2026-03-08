#ifndef GPU_BUFFER_H
#define GPU_BUFFER_H

#include <glad/glad.h>
#include "Mesh.h"

class GPUBuffer {
public:
    GPUBuffer();
    ~GPUBuffer();

    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer& operator=(const GPUBuffer&) = delete;

    GPUBuffer(GPUBuffer&& other) noexcept;
    GPUBuffer& operator=(GPUBuffer&& other) noexcept;

    void uploadMesh(const Mesh& mesh);
    
    void bindVertexBuffer(GLuint bindingPoint) const;
    void bindFaceBuffer(GLuint bindingPoint) const;

    GLuint getVertexBufferID() const { return m_vertexBufferID; }
    GLuint getFaceBufferID() const { return m_faceBufferID; }
    
    size_t getVertexCount() const { return m_vertexCount; }
    size_t getFaceCount() const { return m_faceCount; }

private:
    GLuint m_vertexBufferID;
    GLuint m_faceBufferID;
    size_t m_vertexCount;
    size_t m_faceCount;

    void cleanup();
};

#endif // GPU_BUFFER_H
