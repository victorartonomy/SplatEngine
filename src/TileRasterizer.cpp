#include "TileRasterizer.h"
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>

// Constructor — computes tile grid dimensions from screen size and logs layout info.
// Does not allocate any GL buffers; call initialize() after construction.
//
// Tile count uses ceiling division (screen + TILE_SIZE - 1) / TILE_SIZE so that partial
// tiles at the screen right/bottom edges are included. This matches the AABB clamping
// logic in pass1_count.comp, which clamps to [0, numTilesX/Y].
TileRasterizer::TileRasterizer(int screenWidth, int screenHeight)
    : m_screenWidth(screenWidth)
    , m_screenHeight(screenHeight)
    , m_numTilesX((screenWidth  + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE)
    , m_numTilesY((screenHeight + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE)
    , m_totalTiles(m_numTilesX * m_numTilesY)
    , m_totalTriangleTilePairs(0)
    , m_projectedTriangleBuffer(0)
    , m_tileCounterBuffer(0)
    , m_tileOffsetBuffer(0)
    , m_triangleListBuffer(0)
    , m_blockSumBuffer(0)
    , m_tileWriteOffsetBuffer(0)
    , m_triangleListCapacity(0)
{
    std::cout << "[INFO] Tile-based rasterizer initialized:" << std::endl;
    std::cout << "       Screen: " << m_screenWidth << "×" << m_screenHeight << std::endl;
    std::cout << "       Tiles: " << m_numTilesX << "×" << m_numTilesY << " = " << m_totalTiles << " tiles" << std::endl;
    std::cout << "       Tile size: " << TileInfo::TILE_SIZE << "×" << TileInfo::TILE_SIZE << " pixels" << std::endl;
}

// Destructor — frees all allocated GL buffer objects.
TileRasterizer::~TileRasterizer() {
    cleanup();
}

// Allocate or reallocate all 6 SSBOs for the tiled rendering pipeline.
// Any previously allocated buffers are freed first via cleanup().
//
// Buffer inventory and binding assignments (as used in Renderer.cpp):
//   m_projectedTriangleBuffer — pass1 output: one ProjectedTriangle per input face (binding 2)
//   m_tileCounterBuffer       — pass1 output: atomic per-tile triangle count (binding 3)
//   m_tileOffsetBuffer        — pass2 output: exclusive prefix sum + total in last slot (binding 1)
//   m_blockSumBuffer          — pass2 intermediate: per-workgroup block sums for multi-block scan (binding 2)
//   m_tileWriteOffsetBuffer   — pass3 input: per-tile write cursor, reset to 0 before each bin pass (binding 4)
//   m_triangleListBuffer      — pass3 output / pass4 input: flat triangle ID list (binding 3 / 6)
//
// The triangle list is pre-allocated to maxTriangles * 8 entries based on the assumption
// that the average triangle covers 8 tiles. pass2_prefixSum() reallocates it if the actual
// count exceeds capacity.
void TileRasterizer::initialize(size_t maxTriangles) {
    cleanup();

    // Projected triangle buffer: one entry per input triangle, written by pass1
    size_t projectedBufferSize = maxTriangles * sizeof(ProjectedTriangle);
    std::cout << "[INFO] Allocating projected triangle buffer: " << maxTriangles
              << " triangles (" << (projectedBufferSize / 1024 / 1024) << " MB)" << std::endl;

    glGenBuffers(1, &m_projectedTriangleBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_projectedTriangleBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, projectedBufferSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Tile counter buffer: one uint per tile; atomically incremented by pass1 for each
    // triangle whose AABB overlaps the tile
    size_t tileCounterSize = m_totalTiles * sizeof(GLuint);
    std::cout << "[INFO] Allocating tile counter buffer: " << m_totalTiles
              << " tiles (" << (tileCounterSize / 1024) << " KB)" << std::endl;

    glGenBuffers(1, &m_tileCounterBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileCounterBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileCounterSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Tile offset buffer: prefix sum output.
    // Has totalTiles + 1 slots: slot[i] = starting offset in the triangle list for tile i.
    // The extra slot at index totalTiles holds the grand total (sum of all tile counts).
    size_t tileOffsetSize = (m_totalTiles + 1) * sizeof(GLuint);
    glGenBuffers(1, &m_tileOffsetBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileOffsetBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileOffsetSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Block sum buffer: intermediate values for the multi-block Blelloch prefix sum.
    // One entry per workgroup in the scan (ceil(totalTiles / 256)).
    // In pass2 the three dispatches are: (1) local scan + store block sums,
    // (2) scan the block sums (single group), (3) add block sums back to local offsets.
    GLuint numScanGroups = (m_totalTiles + 255) / 256;
    size_t blockSumSize = numScanGroups * sizeof(GLuint);
    glGenBuffers(1, &m_blockSumBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_blockSumBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, blockSumSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Tile write offset buffer: per-tile atomic write cursor for the binning pass (pass3).
    // Initialized to the prefix sum values at the start of each binning pass so each tile's
    // triangles are written contiguously starting from their designated offset.
    size_t tileWriteOffsetSize = m_totalTiles * sizeof(GLuint);
    glGenBuffers(1, &m_tileWriteOffsetBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileWriteOffsetBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileWriteOffsetSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    std::cout << "[INFO] Tile-based buffers created successfully" << std::endl;

    // Triangle list buffer: pre-allocated to maxTriangles * 8 assuming ~8 tiles per triangle.
    // Will be reallocated in pass2_prefixSum() if the actual count exceeds this estimate.
    m_triangleListCapacity = maxTriangles * 8;
    size_t triangleListSize = m_triangleListCapacity * sizeof(GLuint);
    glGenBuffers(1, &m_triangleListBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleListBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangleListSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    std::cout << "[INFO] Triangle list buffer pre-allocated: " << m_triangleListCapacity
              << " entries (" << (triangleListSize / 1024 / 1024) << " MB)" << std::endl;
}

// Resize the tile rasterizer for a new screen resolution.
// Updates tile grid dimensions, then calls initialize() to reallocate all buffers.
void TileRasterizer::resize(int newWidth, int newHeight, size_t maxTriangles) {
    m_screenWidth  = newWidth;
    m_screenHeight = newHeight;
    m_numTilesX = (newWidth  + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE;
    m_numTilesY = (newHeight + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE;
    m_totalTiles = m_numTilesX * m_numTilesY;

    initialize(maxTriangles);

    std::cout << "[INFO] TileRasterizer resized: " << newWidth << "x" << newHeight
              << " (" << m_numTilesX << "x" << m_numTilesY << " = " << m_totalTiles << " tiles)" << std::endl;
}

// CPU-side setup for pass1: reset the tile counter buffer to zero before dispatching
// pass1_count.comp. Each tile counter must start at 0 so the atomic increments in the
// shader give the correct count for the current frame.
// The actual compute dispatch happens in Renderer::render().
void TileRasterizer::pass1_countTrianglesPerTile(const glm::mat4& viewProjection, size_t triangleCount) {
    std::vector<GLuint> zeros(m_totalTiles, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileCounterBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, m_totalTiles * sizeof(GLuint), zeros.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// CPU-side readback after pass2_prefix_sum.comp has run: read the total triangle-tile pair
// count from the last slot of the tile offset buffer (index totalTiles).
//
// Safety cap: clamp to 64M pairs (= 256 MB of uint indices) to prevent GPU memory exhaustion
// or a TDR timeout on scenes with many large triangles covering the whole screen.
//
// Dynamic reallocation: if the actual pair count exceeds the pre-allocated triangle list capacity,
// reallocate with 2× the required capacity to amortize future resizes.
//
// Returns the total pair count, or 0 if nothing needs to be rasterized this frame.
size_t TileRasterizer::pass2_prefixSum() {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileOffsetBuffer);

    // Read the total from the extra slot at index totalTiles (written by pass2 shader)
    GLuint totalPairs = 0;
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                       m_totalTiles * sizeof(GLuint),  // Byte offset to the last slot
                       sizeof(GLuint),
                       &totalPairs);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Safety cap: 64M pairs = 256 MB; prevents OOM / GPU crash on pathological scenes
    const GLuint MAX_PAIRS = 64 * 1024 * 1024;
    if (totalPairs > MAX_PAIRS) {
        std::cerr << "[WARNING] totalPairs (" << totalPairs << ") exceeds max, capping to " << MAX_PAIRS << std::endl;
        totalPairs = MAX_PAIRS;
    }

    m_totalTriangleTilePairs = totalPairs;

    if (totalPairs == 0) {
        return 0; // No visible geometry — skip bin and rasterize passes
    }

    // Grow the triangle list buffer if the current frame's pair count exceeds capacity.
    // Factor of 2× avoids frequent reallocations when the scene is gradually growing.
    if (totalPairs > m_triangleListCapacity) {
        size_t newCapacity = static_cast<size_t>(totalPairs) * 2;
        size_t newSize = newCapacity * sizeof(GLuint);

        if (m_triangleListBuffer != 0) {
            glDeleteBuffers(1, &m_triangleListBuffer);
        }

        glGenBuffers(1, &m_triangleListBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleListBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        m_triangleListCapacity = newCapacity;
    }

    return totalPairs;
}

// CPU-side setup for pass3: reset the tile write offset buffer to zero before dispatching
// pass3_bin.comp. Each tile's write cursor starts at 0; the shader will add the prefix-sum
// offset stored in the tile offset buffer to determine where in the triangle list to write.
// The actual compute dispatch happens in Renderer::render().
void TileRasterizer::pass3_binTriangles(size_t triangleCount) {
    std::vector<GLuint> zeros(m_totalTiles, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileWriteOffsetBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, m_totalTiles * sizeof(GLuint), zeros.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Free all 6 GL buffer objects and reset their handles to 0.
// Safe to call multiple times (guards on handle != 0).
// Called from the destructor and before reinitializing on resize.
void TileRasterizer::cleanup() {
    if (m_projectedTriangleBuffer != 0) {
        glDeleteBuffers(1, &m_projectedTriangleBuffer);
        m_projectedTriangleBuffer = 0;
    }

    if (m_tileCounterBuffer != 0) {
        glDeleteBuffers(1, &m_tileCounterBuffer);
        m_tileCounterBuffer = 0;
    }

    if (m_tileOffsetBuffer != 0) {
        glDeleteBuffers(1, &m_tileOffsetBuffer);
        m_tileOffsetBuffer = 0;
    }

    if (m_triangleListBuffer != 0) {
        glDeleteBuffers(1, &m_triangleListBuffer);
        m_triangleListBuffer = 0;
    }

    if (m_blockSumBuffer != 0) {
        glDeleteBuffers(1, &m_blockSumBuffer);
        m_blockSumBuffer = 0;
    }

    if (m_tileWriteOffsetBuffer != 0) {
        glDeleteBuffers(1, &m_tileWriteOffsetBuffer);
        m_tileWriteOffsetBuffer = 0;
    }
}
