#include "GPUBuffer.h"
#include <iostream>
#include <chrono>

GPUBuffer::GPUBuffer()
    : m_vertexBufferID(0)
    , m_faceBufferID(0)
    , m_vertexCount(0)
    , m_faceCount(0)
{
}

GPUBuffer::~GPUBuffer() {
    cleanup();
}

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

void GPUBuffer::uploadMesh(const Mesh& mesh) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    cleanup();

    m_vertexCount = mesh.getVertexCount();
    m_faceCount = mesh.getFaceCount();

    std::cout << "[INFO] Uploading mesh to GPU..." << std::endl;
    
    glGenBuffers(1, &m_vertexBufferID);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_vertexBufferID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 
                 mesh.getVertexBufferSize(), 
                 mesh.vertices.data(), 
                 GL_STATIC_DRAW);
    
    std::cout << "[INFO] Vertex buffer uploaded: " << m_vertexCount << " vertices ("
              << (mesh.getVertexBufferSize() / 1024 / 1024) << " MB)" << std::endl;

    glGenBuffers(1, &m_faceBufferID);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_faceBufferID);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 
                 mesh.getFaceBufferSize(), 
                 mesh.faces.data(), 
                 GL_STATIC_DRAW);
    
    std::cout << "[INFO] Face buffer uploaded: " << m_faceCount << " faces ("
              << (mesh.getFaceBufferSize() / 1024 / 1024) << " MB)" << std::endl;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "[INFO] GPU upload completed in " << duration.count() << " ms" << std::endl;
}

void GPUBuffer::bindVertexBuffer(GLuint bindingPoint) const {
    if (m_vertexBufferID != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_vertexBufferID);
    }
}

void GPUBuffer::bindFaceBuffer(GLuint bindingPoint) const {
    if (m_faceBufferID != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_faceBufferID);
    }
}

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
