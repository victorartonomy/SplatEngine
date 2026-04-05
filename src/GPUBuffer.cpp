#include "GPUBuffer.h"
#include <iostream>
#include <chrono>

// Default constructor — initializes all GL handles and counts to zero.
// No GL objects are created until uploadMesh() is called.
GPUBuffer::GPUBuffer()
    : m_vertexBufferID(0)
    , m_faceBufferID(0)
    , m_vertexCount(0)
    , m_faceCount(0)
{
}

// Destructor — frees any allocated GL buffer objects.
GPUBuffer::~GPUBuffer() {
    cleanup();
}

// Move constructor — transfers GL buffer ownership from `other`.
// Zeros out `other` so its destructor does not double-free the buffers.
GPUBuffer::GPUBuffer(GPUBuffer&& other) noexcept
    : m_vertexBufferID(other.m_vertexBufferID)
    , m_faceBufferID(other.m_faceBufferID)
    , m_vertexCount(other.m_vertexCount)
    , m_faceCount(other.m_faceCount)
{
    other.m_vertexBufferID = 0;
    other.m_faceBufferID = 0;
    other.m_vertexCount = 0;
    other.m_faceCount = 0;
}

// Move assignment — releases current buffers, then steals ownership from `other`.
// Self-assignment guard prevents freeing a buffer we are about to use.
GPUBuffer& GPUBuffer::operator=(GPUBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_vertexBufferID = other.m_vertexBufferID;
        m_faceBufferID = other.m_faceBufferID;
        m_vertexCount = other.m_vertexCount;
        m_faceCount = other.m_faceCount;

        other.m_vertexBufferID = 0;
        other.m_faceBufferID = 0;
        other.m_vertexCount = 0;
        other.m_faceCount = 0;
    }
    return *this;
}

// Upload a Mesh to two separate SSBOs: one for vertices (binding 0) and one for
// faces (binding 1). Any previously allocated buffers are freed first via cleanup().
//
// GL_STATIC_DRAW is used because mesh geometry does not change after loading —
// the driver can place these buffers in VRAM rather than shared host/device memory.
//
// Mesh data is uploaded as raw bytes matching the std430-compatible Vertex/Face
// structs defined in Mesh.h. The shaders read from these SSBOs at binding points 0 and 1.
void GPUBuffer::uploadMesh(const Mesh& mesh) {
    auto startTime = std::chrono::high_resolution_clock::now();

    // Release any existing buffers before allocating new ones
    cleanup();

    m_vertexCount = mesh.getVertexCount();
    m_faceCount = mesh.getFaceCount();

    std::cout << "[INFO] Uploading mesh to GPU..." << std::endl;

    // Allocate and fill the vertex SSBO (binding 0 in all compute shaders)
    glGenBuffers(1, &m_vertexBufferID);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_vertexBufferID);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 mesh.getVertexBufferSize(),
                 mesh.vertices.data(),
                 GL_STATIC_DRAW);  // Static — mesh geometry is immutable after upload

    std::cout << "[INFO] Vertex buffer uploaded: " << m_vertexCount << " vertices ("
              << (mesh.getVertexBufferSize() / 1024 / 1024) << " MB)" << std::endl;

    // Allocate and fill the face SSBO (binding 1 in all compute shaders)
    glGenBuffers(1, &m_faceBufferID);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_faceBufferID);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 mesh.getFaceBufferSize(),
                 mesh.faces.data(),
                 GL_STATIC_DRAW);  // Static — face indices don't change after loading

    std::cout << "[INFO] Face buffer uploaded: " << m_faceCount << " faces ("
              << (mesh.getFaceBufferSize() / 1024 / 1024) << " MB)" << std::endl;

    // Unbind to avoid accidental modifications from subsequent GL calls
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "[INFO] GPU upload completed in " << duration.count() << " ms" << std::endl;
}

// Bind the vertex SSBO to the given shader storage binding point.
// Call before dispatching any compute pass that reads from binding 0 (pass1/pass4).
// Guard against m_vertexBufferID == 0 (no mesh uploaded yet).
void GPUBuffer::bindVertexBuffer(GLuint bindingPoint) const {
    if (m_vertexBufferID != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_vertexBufferID);
    }
}

// Bind the face SSBO to the given shader storage binding point.
// Call before dispatching any compute pass that reads from binding 1 (pass4).
// Guard against m_faceBufferID == 0 (no mesh uploaded yet).
void GPUBuffer::bindFaceBuffer(GLuint bindingPoint) const {
    if (m_faceBufferID != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_faceBufferID);
    }
}

// Free both GL buffer objects and reset all state to zero.
// Safe to call even if no buffers have been allocated (0-handle guard).
// Called from the destructor and before re-uploading a new mesh.
void GPUBuffer::cleanup() {
    if (m_vertexBufferID != 0) {
        glDeleteBuffers(1, &m_vertexBufferID);
        m_vertexBufferID = 0;
    }

    if (m_faceBufferID != 0) {
        glDeleteBuffers(1, &m_faceBufferID);
        m_faceBufferID = 0;
    }

    m_vertexCount = 0;
    m_faceCount = 0;
}
