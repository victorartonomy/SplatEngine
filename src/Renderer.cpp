#include "Renderer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>

// ============================================
// HELPER METHODS
// ============================================

GLuint Renderer::createOutputTexture(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint Renderer::createDepthBuffer(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// ============================================
// INITIALIZE
// ============================================

bool Renderer::initialize(int viewportWidth, int viewportHeight) {
    m_clearDepthShader = std::make_unique<ComputeShader>("shaders/clear_depth.comp");
    m_clearTilesShader = std::make_unique<ComputeShader>("shaders/clear_tiles.comp");
    m_pass1Shader      = std::make_unique<ComputeShader>("shaders/pass1_count.comp");
    m_pass2Shader      = std::make_unique<ComputeShader>("shaders/pass2_prefix_sum.comp");
    m_pass3Shader      = std::make_unique<ComputeShader>("shaders/pass3_bin.comp");
    m_pass4Shader      = std::make_unique<ComputeShader>("shaders/pass4_rasterize_tiled.comp");
    m_debugTilesShader = std::make_unique<ComputeShader>("shaders/debug_tiles.comp");

    if (!m_clearDepthShader->isValid() || !m_clearTilesShader->isValid() ||
        !m_pass1Shader->isValid()      || !m_pass2Shader->isValid()      ||
        !m_pass3Shader->isValid()      || !m_pass4Shader->isValid()      ||
        !m_debugTilesShader->isValid()) {
        std::cerr << "[ERROR] Renderer: one or more compute shaders failed to compile" << std::endl;
        return false;
    }

    m_viewportWidth  = viewportWidth;
    m_viewportHeight = viewportHeight;
    m_outputTexture  = createOutputTexture(viewportWidth, viewportHeight);
    m_depthBuffer    = createDepthBuffer(viewportWidth, viewportHeight);

    m_tileRasterizer = std::make_unique<TileRasterizer>(viewportWidth, viewportHeight);
    m_tileRasterizer->initialize(1);

    return true;
}

// ============================================
// SHUTDOWN
// ============================================

void Renderer::shutdown() {
    glDeleteTextures(1, &m_outputTexture); m_outputTexture = 0;
    glDeleteTextures(1, &m_depthBuffer);   m_depthBuffer   = 0;

    m_tileRasterizer.reset();
    m_clearDepthShader.reset();
    m_clearTilesShader.reset();
    m_pass1Shader.reset();
    m_pass2Shader.reset();
    m_pass3Shader.reset();
    m_pass4Shader.reset();
    m_debugTilesShader.reset();
}

// ============================================
// RESIZE
// ============================================

void Renderer::resize(int newWidth, int newHeight) {
    glFinish();
    glDeleteTextures(1, &m_outputTexture);
    glDeleteTextures(1, &m_depthBuffer);
    m_viewportWidth  = newWidth;
    m_viewportHeight = newHeight;
    m_outputTexture  = createOutputTexture(newWidth, newHeight);
    m_depthBuffer    = createDepthBuffer(newWidth, newHeight);
    if (!m_mergedMesh.faces.empty())
        m_tileRasterizer->resize(newWidth, newHeight, m_mergedMesh.getFaceCount());
}

// ============================================
// SUBMIT SCENE
// ============================================

void Renderer::submitScene(Scene& scene) {
    m_mergedMesh.vertices.clear();
    m_mergedMesh.faces.clear();

    for (const Entity& entity : scene.getEntities()) {
        if (!entity.visible || entity.meshAssetIndex < 0)
            continue;
        const MeshAsset* asset = scene.getMeshAsset(entity.meshAssetIndex);
        if (!asset)
            continue;

        size_t vertexOffset = m_mergedMesh.vertices.size();
        glm::mat4 model = entity.transform.getModelMatrix();

        for (const Vertex& v : asset->mesh.vertices) {
            Vertex transformed   = v;
            transformed.position = glm::vec3(model * glm::vec4(v.position, 1.0f));
            m_mergedMesh.vertices.push_back(transformed);
        }

        for (Face f : asset->mesh.faces) {
            f.indices += glm::uvec3(static_cast<unsigned int>(vertexOffset));
            m_mergedMesh.faces.push_back(f);
        }
    }

    if (!m_mergedMesh.faces.empty()) {
        glFinish();
        m_mergedGpuBuffer.uploadMesh(m_mergedMesh);
        m_tileRasterizer->resize(m_viewportWidth, m_viewportHeight,
                                 m_mergedMesh.getFaceCount());
    }

    m_sceneDirty = false;
}

// ============================================
// RENDER
// ============================================

void Renderer::render(Scene& scene, const Camera& camera) {
    if (m_sceneDirty)
        submitScene(scene);

    if (!scene.hasVisibleMeshes() ||
        m_mergedMesh.getFaceCount() == 0 ||
        m_viewportWidth <= 0 || m_viewportHeight <= 0)
        return;

    GLuint numGroupsX = (m_viewportWidth  + 15) / 16;
    GLuint numGroupsY = (m_viewportHeight + 15) / 16;
    float  aspectRatio = static_cast<float>(m_viewportWidth) /
                         static_cast<float>(m_viewportHeight);
    glm::mat4 viewProjection =
        camera.getViewProjectionMatrix(aspectRatio, camera.getFOV());

    // --- Phase 0: Clear ---
    m_clearDepthShader->use();
    glBindImageTexture(0, m_depthBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    m_clearDepthShader->dispatch(numGroupsX, numGroupsY, 1);

    m_clearTilesShader->use();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                     m_tileRasterizer->getTileCounterBufferID());
    m_clearTilesShader->setUInt("numElements",
                                static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    GLuint numTileGroups = (m_tileRasterizer->getTotalTiles() + 255) / 256;
    m_clearTilesShader->dispatch(numTileGroups, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // --- Phase 1: Project & Count ---
    m_pass1Shader->use();
    m_mergedGpuBuffer.bindVertexBuffer(0);
    m_mergedGpuBuffer.bindFaceBuffer(1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                     m_tileRasterizer->getProjectedTriangleBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,
                     m_tileRasterizer->getTileCounterBufferID());

    m_pass1Shader->setMat4("viewProjection", viewProjection);
    m_pass1Shader->setIVec2("screenSize",
                            glm::ivec2(m_viewportWidth, m_viewportHeight));
    m_pass1Shader->setIVec2("numTiles",
                            glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                       m_tileRasterizer->getNumTilesY()));

    GLuint numTriangleGroups =
        (static_cast<GLuint>(m_mergedMesh.getFaceCount()) + 255) / 256;
    m_pass1Shader->dispatch(numTriangleGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // --- Phase 1.5: Prefix Sum ---
    m_pass2Shader->use();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                     m_tileRasterizer->getTileCounterBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                     m_tileRasterizer->getTileOffsetBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                     m_tileRasterizer->getBlockSumBufferID());

    GLuint numScanGroups = (m_tileRasterizer->getTotalTiles() + 255) / 256;

    m_pass2Shader->setUInt("numElements",
                           static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    m_pass2Shader->setUInt("pass", 0u);
    m_pass2Shader->dispatch(numScanGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    if (numScanGroups > 1) {
        m_pass2Shader->setUInt("numElements",
                               static_cast<GLuint>(numScanGroups));
        m_pass2Shader->setUInt("pass", 1u);
        m_pass2Shader->dispatch(1, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    m_pass2Shader->setUInt("numElements",
                           static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    m_pass2Shader->setUInt("pass", 2u);
    m_pass2Shader->dispatch(numScanGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    size_t totalPairs = m_tileRasterizer->pass2_prefixSum();

    // --- Phase 2: Bin ---
    if (totalPairs > 0) {
        m_tileRasterizer->pass3_binTriangles(m_mergedMesh.getFaceCount());

        m_pass3Shader->use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                         m_tileRasterizer->getProjectedTriangleBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                         m_tileRasterizer->getTileCounterBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                         m_tileRasterizer->getTileOffsetBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,
                         m_tileRasterizer->getTriangleListBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,
                         m_tileRasterizer->getTileWriteOffsetBufferID());

        m_pass3Shader->setIVec2("numTiles",
                                glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                           m_tileRasterizer->getNumTilesY()));
        m_pass3Shader->setUInt("maxTriangleListSize",
                               static_cast<GLuint>(
                                   m_tileRasterizer->getTotalTriangleTilePairs()));
        m_pass3Shader->dispatch(numTriangleGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // --- Phase 3: Rasterize ---
    if (m_debugMode) {
        m_debugTilesShader->use();
        glBindImageTexture(0, m_outputTexture, 0, GL_FALSE, 0,
                           GL_WRITE_ONLY, GL_RGBA32F);
        m_debugTilesShader->setIVec2("screenSize",
                                     glm::ivec2(m_viewportWidth, m_viewportHeight));
        m_debugTilesShader->dispatch(numGroupsX, numGroupsY, 1);
    } else if (totalPairs > 0) {
        m_pass4Shader->use();
        glBindImageTexture(0, m_outputTexture, 0, GL_FALSE, 0,
                           GL_WRITE_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_depthBuffer, 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R32UI);
        m_mergedGpuBuffer.bindVertexBuffer(2);
        m_mergedGpuBuffer.bindFaceBuffer(3);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,
                         m_tileRasterizer->getProjectedTriangleBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5,
                         m_tileRasterizer->getTileOffsetBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6,
                         m_tileRasterizer->getTriangleListBufferID());

        m_pass4Shader->setIVec2("screenSize",
                                glm::ivec2(m_viewportWidth, m_viewportHeight));
        m_pass4Shader->setIVec2("numTiles",
                                glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                           m_tileRasterizer->getNumTilesY()));
        m_pass4Shader->dispatch(numGroupsX, numGroupsY, 1);
    } else {
        // No visible geometry — fill with dark background
        glBindTexture(GL_TEXTURE_2D, m_outputTexture);
        std::vector<float> bg(m_viewportWidth * m_viewportHeight * 4);
        for (int i = 0; i < m_viewportWidth * m_viewportHeight; i++) {
            bg[i * 4 + 0] = 0.1f;
            bg[i * 4 + 1] = 0.1f;
            bg[i * 4 + 2] = 0.1f;
            bg[i * 4 + 3] = 1.0f;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        m_viewportWidth, m_viewportHeight,
                        GL_RGBA, GL_FLOAT, bg.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Ensure compute writes are visible before ImGui samples the texture
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}
