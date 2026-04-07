#include "Renderer.h"
#include "AssetManager.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>

// ============================================
// HELPER METHODS
// ============================================

// Create the RGBA32F output texture that the rasterize pass writes to via imageStore.
// GL_RGBA32F gives full HDR range; ImGui samples it as a viewport texture each frame.
// GL_LINEAR filtering enables sub-pixel smoothing when the viewport is resized.
// GL_CLAMP_TO_EDGE prevents border artifacts at texture edges.
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

// Create the R32UI depth buffer texture.
// Each pixel stores a 32-bit unsigned integer encoding of an NDC depth value.
// imageAtomicMin is used in pass4 to do atomic per-pixel depth testing without a hardware
// depth buffer (which compute shaders cannot use). GL_NEAREST is required because GL_LINEAR
// is not defined for integer textures.
// The clear pass writes 0xFFFFFFFF (maximum uint = farthest depth) to all pixels.
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

// Create the shadow map texture — a square R32UI image used by the shadow pass.
//
// We use R32UI (not R32F) because GLSL's imageAtomicMin only works on integer images.
// shadow_pass.comp bit-casts its float depth values to uints with floatBitsToUint(),
// which is monotonic for non-negative floats, so atomicMin on the bit pattern correctly
// selects the closest light-space surface.
//
// NEAREST filtering is mandatory for integer formats. Clamp-to-edge prevents edge
// fetches from sampling phantom data when a fragment lies just outside the light frustum.
GLuint Renderer::createShadowMap(int size) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, size, size, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// ============================================
// OIT BUFFER HELPER
// ============================================

// Allocate (or reallocate) the five OIT accumulation SSBOs.
// Each buffer holds one uint32 per screen pixel, storing a packed float.
// GL_DYNAMIC_DRAW is used because these buffers are re-allocated on every resize.
// Called from initialize() on startup and from resize() whenever the viewport changes.
void Renderer::createOITBuffers(int width, int height) {
    // Each pixel needs one uint32 (4 bytes) per OIT channel.
    GLsizeiptr bufferSize = static_cast<GLsizeiptr>(width) * height * sizeof(uint32_t);

    // Helper lambda: create on first call, then re-allocate storage each time.
    auto allocBuf = [&](GLuint& buf) {
        if (buf == 0)
            glGenBuffers(1, &buf);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
        // nullptr data — pass4 writes every pixel unconditionally each frame, so
        // we don't need to initialize the contents here.
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    };

    allocBuf(m_oitAccumR);  // sum(r * alpha * weight) per pixel
    allocBuf(m_oitAccumG);  // sum(g * alpha * weight) per pixel
    allocBuf(m_oitAccumB);  // sum(b * alpha * weight) per pixel
    allocBuf(m_oitAccumA);  // sum(alpha * weight) per pixel
    allocBuf(m_oitReveal);  // product(1 - alpha_i) per pixel (transmittance)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// ============================================
// INITIALIZE
// ============================================

// Load all compute shaders, create the output texture and depth buffer, and initialize
// the TileRasterizer with a placeholder size of 1 triangle.
//
// TileRasterizer is initialized with size 1 because no mesh is loaded at startup — it will
// be resized in submitScene() when the first mesh is loaded and its actual face count is known.
//
// Returns false if any shader fails to compile so the caller can abort startup.
bool Renderer::initialize(int viewportWidth, int viewportHeight) {
    // Load all compute shader stages — failures are logged by ComputeShader constructor
    m_clearDepthShader = std::make_unique<ComputeShader>("shaders/clear_depth.comp");
    m_clearTilesShader = std::make_unique<ComputeShader>("shaders/clear_tiles.comp");
    m_pass1Shader      = std::make_unique<ComputeShader>("shaders/pass1_count.comp");
    m_pass2Shader      = std::make_unique<ComputeShader>("shaders/pass2_prefix_sum.comp");
    m_pass3Shader      = std::make_unique<ComputeShader>("shaders/pass3_bin.comp");
    m_pass4Shader      = std::make_unique<ComputeShader>("shaders/pass4_rasterize_tiled.comp");
    // OIT composite pass: blend transparent fragments over the opaque output image.
    m_pass5OITShader   = std::make_unique<ComputeShader>("shaders/pass5_oit_composite.comp");
    m_debugTilesShader = std::make_unique<ComputeShader>("shaders/debug_tiles.comp");
    // Shadow mapping shaders.
    m_clearShadowShader = std::make_unique<ComputeShader>("shaders/clear_shadow.comp");
    m_shadowPassShader  = std::make_unique<ComputeShader>("shaders/shadow_pass.comp");

    if (!m_clearDepthShader->isValid()  || !m_clearTilesShader->isValid()  ||
        !m_pass1Shader->isValid()       || !m_pass2Shader->isValid()       ||
        !m_pass3Shader->isValid()       || !m_pass4Shader->isValid()       ||
        !m_pass5OITShader->isValid()    || !m_debugTilesShader->isValid()  ||
        !m_clearShadowShader->isValid() || !m_shadowPassShader->isValid()) {
        std::cerr << "[ERROR] Renderer: one or more compute shaders failed to compile" << std::endl;
        return false;
    }

    m_viewportWidth  = viewportWidth;
    m_viewportHeight = viewportHeight;
    m_outputTexture  = createOutputTexture(viewportWidth, viewportHeight);
    m_depthBuffer    = createDepthBuffer(viewportWidth, viewportHeight);

    // Shadow map is fixed-resolution, so it's created once here and never resized
    // in response to viewport changes.
    m_shadowMapTexture = createShadowMap(m_shadowMapSize);

    m_tileRasterizer = std::make_unique<TileRasterizer>(viewportWidth, viewportHeight);
    m_tileRasterizer->initialize(1); // Placeholder — resized on first mesh load

    // Allocate OIT accumulation SSBOs at initial viewport size.
    // These are written unconditionally by pass4 every frame, so no initial data is needed.
    createOITBuffers(viewportWidth, viewportHeight);

    // Upload the default material (slot 0) to the GPU.
    // All faces parsed from COFF files are assigned materialID=0, so existing meshes
    // render identically: face.color × albedo(1,1,1) = face.color.
    m_materialManager.upload();

    // Upload the dummy 1×1 white texture array so pass4's "textureAtlas" sampler uniform
    // is always bound to a complete, valid texture object even before any user textures load.
    m_textureManager.upload();

    return true;
}

// ============================================
// SHUTDOWN
// ============================================

// Release all GPU resources in reverse initialization order.
// LightManager must be shut down before the GL context is destroyed (it owns an SSBO).
// ComputeShader unique_ptrs reset here, calling their destructors and glDeleteProgram.
void Renderer::shutdown() {
    glDeleteTextures(1, &m_outputTexture);    m_outputTexture    = 0;
    glDeleteTextures(1, &m_depthBuffer);      m_depthBuffer      = 0;
    glDeleteTextures(1, &m_shadowMapTexture); m_shadowMapTexture = 0;

    // Release OIT accumulation SSBOs
    GLuint oitBufs[] = { m_oitAccumR, m_oitAccumG, m_oitAccumB, m_oitAccumA, m_oitReveal };
    glDeleteBuffers(5, oitBufs);
    m_oitAccumR = m_oitAccumG = m_oitAccumB = m_oitAccumA = m_oitReveal = 0;

    m_lightManager.shutdown();
    m_materialManager.shutdown();
    m_textureManager.shutdown();
    m_tileRasterizer.reset();
    m_clearDepthShader.reset();
    m_clearTilesShader.reset();
    m_pass1Shader.reset();
    m_pass2Shader.reset();
    m_pass3Shader.reset();
    m_pass4Shader.reset();
    m_pass5OITShader.reset();
    m_debugTilesShader.reset();
    m_clearShadowShader.reset();
    m_shadowPassShader.reset();
}

// ============================================
// RESIZE
// ============================================

// Handle viewport resize by recreating the output texture and depth buffer at the new size.
// glFinish() ensures all in-flight compute work has completed before the old textures are deleted.
// Also resizes TileRasterizer buffer allocations if a mesh is currently loaded.
void Renderer::resize(int newWidth, int newHeight) {
    glFinish(); // Wait for GPU to complete any ongoing compute dispatches
    glDeleteTextures(1, &m_outputTexture);
    glDeleteTextures(1, &m_depthBuffer);
    m_viewportWidth  = newWidth;
    m_viewportHeight = newHeight;
    m_outputTexture  = createOutputTexture(newWidth, newHeight);
    m_depthBuffer    = createDepthBuffer(newWidth, newHeight);
    // OIT buffers are per-pixel so they must match the new viewport dimensions exactly.
    createOITBuffers(newWidth, newHeight);
    if (!m_mergedMesh.faces.empty())
        m_tileRasterizer->resize(newWidth, newHeight, m_mergedMesh.getFaceCount());
}

// ============================================
// SUBMIT SCENE
// ============================================

// Merge all visible mesh entities into a single world-space Mesh and upload to the GPU.
//
// Why a merged mesh?
//   Compute shaders dispatch over all triangles in one draw call. Using a single merged
//   SSBO is simpler and faster than per-entity SSBOs with indirect dispatch — there are no
//   instance transforms to manage in the shader, and all triangle indices are global.
//
// Normal transform:
//   Vertex normals cannot be transformed with the regular model matrix when non-uniform
//   scale is present — the matrix would distort the normal direction. The correct transform
//   is the transpose of the inverse of the 3×3 upper-left of the model matrix:
//     normalMatrix = transpose(inverse(mat3(model)))
//   This preserves the perpendicularity of normals to their surface under non-uniform scale.
//   Normals are re-normalized after transformation to counteract any scaling that remains.
//
// Face index rebasing:
//   Each entity's face indices are relative to its own vertex array starting at 0.
//   After merging, they must be offset by `vertexOffset` (the number of vertices already
//   in the merged array) so they correctly index into the combined vertex buffer.
void Renderer::submitScene(Scene& scene, const AssetManager& am) {
    m_mergedMesh.vertices.clear();
    m_mergedMesh.faces.clear();

    for (const Entity& entity : scene.getEntities()) {
        if (!entity.visible || !entity.meshAsset.isValid())
            continue;
        const MeshAsset* asset = am.get(entity.meshAsset);
        if (!asset)
            continue;

        size_t vertexOffset = m_mergedMesh.vertices.size();
        glm::mat4 model = entity.transform.getModelMatrix();

        // Normal matrix: transpose(inverse(M)) — correct under non-uniform scale
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        for (const Vertex& v : asset->mesh.vertices) {
            Vertex transformed   = v;
            transformed.position = glm::vec3(model * glm::vec4(v.position, 1.0f));
            transformed.normal   = glm::normalize(normalMatrix * v.normal);
            m_mergedMesh.vertices.push_back(transformed);
        }

        // Rebase face indices into the merged vertex array
        for (Face f : asset->mesh.faces) {
            f.indices += glm::uvec3(static_cast<unsigned int>(vertexOffset));
            m_mergedMesh.faces.push_back(f);
        }
    }

    if (!m_mergedMesh.faces.empty()) {
        glFinish(); // Ensure prior frame's GPU reads from the old buffer complete before re-upload
        m_mergedGpuBuffer.uploadMesh(m_mergedMesh);
        m_tileRasterizer->resize(m_viewportWidth, m_viewportHeight,
                                 m_mergedMesh.getFaceCount());
    }

    m_sceneDirty = false;
}

// ============================================
// RENDER
// ============================================

// Execute the full 5-phase tiled compute rasterization pipeline for one frame.
//
// Pipeline overview (see CLAUDE.md for the full table):
//   Phase 0: Clear — reset depth buffer to 0xFFFFFFFF, tile counters to 0
//   Phase 1: Project & Count — project each triangle to screen space, count per tile
//   Phase 1.5: Prefix Sum — Blelloch exclusive scan over tile counters (3 dispatches)
//   Phase 2: Bin — scatter triangle IDs into per-tile lists using prefix sum offsets
//   Phase 3: Rasterize — per-pixel edge test, depth test, lighting, and color write
//
// Early exit conditions:
//   - m_sceneDirty: re-merge and re-upload the scene geometry first
//   - No visible meshes or empty merged mesh: skip all passes
//   - totalPairs == 0 after prefix sum: no triangles overlap any tile (all clipped)
void Renderer::render(Scene& scene, const Camera& camera, const AssetManager& am) {
    if (m_sceneDirty)
        submitScene(scene, am);

    if (!scene.hasVisibleMeshes() ||
        m_mergedMesh.getFaceCount() == 0 ||
        m_viewportWidth <= 0 || m_viewportHeight <= 0)
        return;

    // Workgroup counts: one 16×16 workgroup per tile (matches local_size_x/y = 16 in pass4)
    GLuint numGroupsX = (m_viewportWidth  + 15) / 16;
    GLuint numGroupsY = (m_viewportHeight + 15) / 16;
    float  aspectRatio = static_cast<float>(m_viewportWidth) /
                         static_cast<float>(m_viewportHeight);
    glm::mat4 viewProjection =
        camera.getViewProjectionMatrix(aspectRatio, camera.getFOV());

    // --- Phase 0: Clear ---
    // Reset depth buffer pixels to 0xFFFFFFFF (maximum uint = farthest depth).
    // Reset tile counters to 0 so pass1's atomic increments start from a clean state.

    m_clearDepthShader->use();
    glBindImageTexture(0, m_depthBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    m_clearDepthShader->dispatch(numGroupsX, numGroupsY, 1);

    m_clearTilesShader->use();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_tileRasterizer->getTileCounterBufferID());
    m_clearTilesShader->setUInt("numElements", static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    GLuint numTileGroups = (m_tileRasterizer->getTotalTiles() + 255) / 256;
    m_clearTilesShader->dispatch(numTileGroups, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // --- Phase 0.5: Shadow Pass ---
    // Render scene depth from the perspective of the primary shadow-casting light
    // into m_shadowMapTexture. pass4 will sample this map per-pixel to darken
    // surfaces that are occluded from the light.
    //
    // We rebuild the light SSBO here (rather than later, just before pass4) so we
    // can read the cached light-view-projection matrix immediately. The SSBO is
    // safe to update at this point — no compute pass between here and pass4 reads it.
    m_lightManager.update(scene.getEntities());
    glm::mat4 shadowVP = m_lightManager.getShadowViewProjection();

    // 1) Clear the shadow map texture to 0xFFFFFFFF (= max depth).
    m_clearShadowShader->use();
    glBindImageTexture(0, m_shadowMapTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    GLuint shadowGroups = (m_shadowMapSize + 15) / 16;
    m_clearShadowShader->dispatch(shadowGroups, shadowGroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // 2) Rasterise scene depth from the light's point of view.
    //    One thread per triangle, atomicMin into the shadow map.
    m_shadowPassShader->use();
    m_mergedGpuBuffer.bindVertexBuffer(0); // VertexBuffer at binding 0
    m_mergedGpuBuffer.bindFaceBuffer(1);   // FaceBuffer at binding 1
    glBindImageTexture(2, m_shadowMapTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    m_shadowPassShader->setMat4 ("lightViewProjection", shadowVP);
    m_shadowPassShader->setIVec2("shadowMapSize",
                                 glm::ivec2(m_shadowMapSize, m_shadowMapSize));
    GLuint numShadowTriGroups =
        (static_cast<GLuint>(m_mergedMesh.getFaceCount()) + 255) / 256;
    m_shadowPassShader->dispatch(numShadowTriGroups, 1, 1);
    // Make subsequent reads (in pass4) see the shadow-map writes.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    // --- Phase 1: Project & Count ---
    // Each thread processes one triangle. It projects all 3 vertices to screen space,
    // computes the screen-space AABB, and atomically increments the counter for every tile
    // the AABB overlaps. The projected triangle (including NDC depths) is written to the
    // projected triangle buffer for use in later passes.

    m_pass1Shader->use();
    m_mergedGpuBuffer.bindVertexBuffer(0); // Vertex SSBO at binding 0
    m_mergedGpuBuffer.bindFaceBuffer(1);   // Face SSBO at binding 1
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_tileRasterizer->getProjectedTriangleBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_tileRasterizer->getTileCounterBufferID());

    m_pass1Shader->setMat4("viewProjection", viewProjection);
    m_pass1Shader->setIVec2("screenSize", glm::ivec2(m_viewportWidth, m_viewportHeight));
    m_pass1Shader->setIVec2("numTiles",
                            glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                       m_tileRasterizer->getNumTilesY()));

    // One workgroup per 256 triangles
    GLuint numTriangleGroups =
        (static_cast<GLuint>(m_mergedMesh.getFaceCount()) + 255) / 256;
    m_pass1Shader->dispatch(numTriangleGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // --- Phase 1.5: Prefix Sum (Blelloch Exclusive Scan) ---
    // Convert per-tile counts into per-tile starting offsets in the flat triangle list.
    //
    // Three-pass strategy for multi-block input:
    //   pass=0: Each workgroup scans its 256-element block in shared memory, writes the
    //           block's total to m_blockSumBuffer[groupID].
    //   pass=1: A single workgroup scans m_blockSumBuffer (only needed if numScanGroups > 1).
    //   pass=2: Each workgroup adds its block sum to all local offsets, completing the
    //           global exclusive scan. The grand total ends up in offset[totalTiles].

    m_pass2Shader->use();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_tileRasterizer->getTileCounterBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_tileRasterizer->getTileOffsetBufferID());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_tileRasterizer->getBlockSumBufferID());

    GLuint numScanGroups = (m_tileRasterizer->getTotalTiles() + 255) / 256;

    // Pass 0: local scan + store block sums
    m_pass2Shader->setUInt("numElements", static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    m_pass2Shader->setUInt("pass", 0u);
    m_pass2Shader->dispatch(numScanGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Pass 1: scan the block sums (only needed when there are multiple scan groups)
    if (numScanGroups > 1) {
        m_pass2Shader->setUInt("numElements", static_cast<GLuint>(numScanGroups));
        m_pass2Shader->setUInt("pass", 1u);
        m_pass2Shader->dispatch(1, 1, 1); // Single group — all block sums fit in shared memory
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // Pass 2: add block sums back to produce the global exclusive scan output
    m_pass2Shader->setUInt("numElements", static_cast<GLuint>(m_tileRasterizer->getTotalTiles()));
    m_pass2Shader->setUInt("pass", 2u);
    m_pass2Shader->dispatch(numScanGroups, 1, 1);
    // GL_BUFFER_UPDATE_BARRIER_BIT required because glGetBufferSubData will read the result
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    // CPU readback: read totalPairs from offset[totalTiles], reallocate triangle list if needed
    size_t totalPairs = m_tileRasterizer->pass2_prefixSum();

    // --- Phase 2: Bin ---
    // Scatter triangle IDs into per-tile slots in the triangle list.
    // Each thread processes one triangle and writes its ID to every tile it overlaps,
    // using atomic increments on the tileWriteOffsetBuffer to find its slot.

    if (totalPairs > 0) {
        m_tileRasterizer->pass3_binTriangles(m_mergedMesh.getFaceCount()); // Zero write offsets

        m_pass3Shader->use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_tileRasterizer->getProjectedTriangleBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_tileRasterizer->getTileCounterBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_tileRasterizer->getTileOffsetBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_tileRasterizer->getTriangleListBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_tileRasterizer->getTileWriteOffsetBufferID());

        m_pass3Shader->setIVec2("numTiles",
                                glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                           m_tileRasterizer->getNumTilesY()));
        m_pass3Shader->setUInt("maxTriangleListSize",
                               static_cast<GLuint>(m_tileRasterizer->getTotalTriangleTilePairs()));
        m_pass3Shader->dispatch(numTriangleGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // --- Phase 3: Rasterize ---
    // One workgroup per tile (16×16 = 256 threads). Each thread processes one pixel.
    // For each triangle in the tile's list: edge-function point-in-triangle test,
    // barycentric depth interpolation, imageAtomicMin depth test, then lighting.

    if (m_debugMode) {
        // Debug mode: visualize tile occupancy (triangle count per tile as a heat map)
        m_debugTilesShader->use();
        glBindImageTexture(0, m_outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        m_debugTilesShader->setIVec2("screenSize", glm::ivec2(m_viewportWidth, m_viewportHeight));
        m_debugTilesShader->dispatch(numGroupsX, numGroupsY, 1);
    } else if (totalPairs > 0) {
        // (Light SSBO was already updated above before the shadow pass — don't re-upload.)

        m_pass4Shader->use();
        // Binding layout for pass4_rasterize_tiled.comp:
        //   Image binding 0: output color texture (RGBA32F, write-only)
        //   Image binding 1: depth buffer (R32UI, read-write for atomic min)
        //   SSBO binding 2: vertex buffer
        //   SSBO binding 3: face buffer
        //   SSBO binding 4: projected triangle buffer
        //   SSBO binding 5: tile offset buffer (prefix sum output)
        //   SSBO binding 6: triangle list buffer (binning output)
        //   SSBO binding 7: light buffer (GPULight array)
        glBindImageTexture(0, m_outputTexture,    0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_depthBuffer,      0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        // Shadow map at image unit 2 — image units are a separate namespace from SSBO
        // bindings, and most GPUs cap GL_MAX_IMAGE_UNITS at 8. Units 0/1 already hold
        // the output color and depth images.
        glBindImageTexture(2, m_shadowMapTexture, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_R32UI);
        m_mergedGpuBuffer.bindVertexBuffer(2);
        m_mergedGpuBuffer.bindFaceBuffer(3);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_tileRasterizer->getProjectedTriangleBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_tileRasterizer->getTileOffsetBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_tileRasterizer->getTriangleListBufferID());
        m_lightManager.bind(7);
        m_materialManager.bind(8);  // binding 8: GPUMaterial array

        // OIT accumulation SSBOs (bindings 9-13).
        // pass4 writes thread-local WBOIT data for every in-bounds pixel at the end of main().
        // No explicit clear is needed — each pixel thread initializes its own local accumulators.
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  m_oitAccumR);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_oitAccumG);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_oitAccumB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_oitAccumA);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, m_oitReveal);

        // Bind the texture array to unit 0 and point the sampler uniform at it.
        m_textureManager.bind(0);
        m_pass4Shader->setInt("textureAtlas", 0);

        m_pass4Shader->setIVec2("screenSize",
                                glm::ivec2(m_viewportWidth, m_viewportHeight));
        m_pass4Shader->setIVec2("numTiles",
                                glm::ivec2(m_tileRasterizer->getNumTilesX(),
                                           m_tileRasterizer->getNumTilesY()));
        m_pass4Shader->setInt("numLights",    m_lightManager.getLightCount());
        m_pass4Shader->setInt("numMaterials", m_materialManager.getMaterialCount());
        m_pass4Shader->setVec3("cameraPos",   camera.getPosition());
        m_pass4Shader->setInt("shadingModel", static_cast<int>(m_lightManager.getShadingModel()));

        // Shadow mapping uniforms — pass4 uses these to project worldPos into the
        // shadow map and apply a binary occlusion factor to the matching light.
        m_pass4Shader->setMat4 ("shadowViewProjection", shadowVP);
        m_pass4Shader->setIVec2("shadowMapSize",
                                glm::ivec2(m_shadowMapSize, m_shadowMapSize));
        m_pass4Shader->setInt  ("shadowLightIndex", m_lightManager.getShadowLightIndex());

        m_pass4Shader->dispatch(numGroupsX, numGroupsY, 1);

        // Ensure all pass4 SSBO writes (OIT accumulators) and image writes (depth + output)
        // are visible before pass5 reads them.
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // --- Phase 4: OIT Composite (pass5_oit_composite.comp) ---
        // For every pixel, read the accumulated transparent fragment data from the OIT SSBOs
        // and blend it over the opaque color already in the output image.
        // Only pixels with actual transparent coverage (oitAccumA > 0) are modified.
        m_pass5OITShader->use();
        // outputImage at image unit 0 — needs both read (opaque color) and write (final composite)
        glBindImageTexture(0, m_outputTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  m_oitAccumR);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_oitAccumG);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_oitAccumB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_oitAccumA);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, m_oitReveal);
        m_pass5OITShader->setIVec2("screenSize", glm::ivec2(m_viewportWidth, m_viewportHeight));
        m_pass5OITShader->dispatch(numGroupsX, numGroupsY, 1);
    } else {
        // No visible geometry — fill with a dark background color via CPU upload.
        // This avoids leaving stale content in the output texture from a previous frame.
        glBindTexture(GL_TEXTURE_2D, m_outputTexture);
        std::vector<float> bg(m_viewportWidth * m_viewportHeight * 4);
        for (int i = 0; i < m_viewportWidth * m_viewportHeight; i++) {
            bg[i * 4 + 0] = 0.1f;  // R
            bg[i * 4 + 1] = 0.1f;  // G
            bg[i * 4 + 2] = 0.1f;  // B
            bg[i * 4 + 3] = 1.0f;  // A
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        m_viewportWidth, m_viewportHeight,
                        GL_RGBA, GL_FLOAT, bg.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // GL_TEXTURE_FETCH_BARRIER_BIT ensures that imageStore writes from pass4 are visible
    // when ImGui samples the texture with a texture fetch (glBindTexture / GLSL texture()).
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}
