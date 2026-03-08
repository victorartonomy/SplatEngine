#include "TileRasterizer.h"
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>

TileRasterizer::TileRasterizer(int screenWidth, int screenHeight)
    : m_screenWidth(screenWidth)
    , m_screenHeight(screenHeight)
    , m_numTilesX((screenWidth + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE)
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

TileRasterizer::~TileRasterizer() {
    cleanup();
}

void TileRasterizer::initialize(size_t maxTriangles) {
    cleanup();

    size_t projectedBufferSize = maxTriangles * sizeof(ProjectedTriangle);
    std::cout << "[INFO] Allocating projected triangle buffer: " << maxTriangles 
              << " triangles (" << (projectedBufferSize / 1024 / 1024) << " MB)" << std::endl;

    glGenBuffers(1, &m_projectedTriangleBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_projectedTriangleBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, projectedBufferSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    size_t tileCounterSize = m_totalTiles * sizeof(GLuint);
    std::cout << "[INFO] Allocating tile counter buffer: " << m_totalTiles 
              << " tiles (" << (tileCounterSize / 1024) << " KB)" << std::endl;

    glGenBuffers(1, &m_tileCounterBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileCounterBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileCounterSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Tile offset buffer: prefix sum output (totalTiles + 1 entries, last holds the total)
    size_t tileOffsetSize = (m_totalTiles + 1) * sizeof(GLuint);
    glGenBuffers(1, &m_tileOffsetBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileOffsetBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileOffsetSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Block sum buffer: for multi-block prefix sum
    GLuint numScanGroups = (m_totalTiles + 255) / 256;
    size_t blockSumSize = numScanGroups * sizeof(GLuint);
    glGenBuffers(1, &m_blockSumBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_blockSumBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, blockSumSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Tile write offset buffer: per-tile atomic write counters for binning pass
    size_t tileWriteOffsetSize = m_totalTiles * sizeof(GLuint);
    glGenBuffers(1, &m_tileWriteOffsetBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileWriteOffsetBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, tileWriteOffsetSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    std::cout << "[INFO] Tile-based buffers created successfully" << std::endl;

    // Pre-allocate triangle list buffer (assume avg 8 tiles per triangle)
    m_triangleListCapacity = maxTriangles * 8;
    size_t triangleListSize = m_triangleListCapacity * sizeof(GLuint);
    glGenBuffers(1, &m_triangleListBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleListBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, triangleListSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    std::cout << "[INFO] Triangle list buffer pre-allocated: " << m_triangleListCapacity
              << " entries (" << (triangleListSize / 1024 / 1024) << " MB)" << std::endl;
}

void TileRasterizer::resize(int newWidth, int newHeight, size_t maxTriangles) {
    m_screenWidth = newWidth;
    m_screenHeight = newHeight;
    m_numTilesX = (newWidth + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE;
    m_numTilesY = (newHeight + TileInfo::TILE_SIZE - 1) / TileInfo::TILE_SIZE;
    m_totalTiles = m_numTilesX * m_numTilesY;

    initialize(maxTriangles);

    std::cout << "[INFO] TileRasterizer resized: " << newWidth << "x" << newHeight
              << " (" << m_numTilesX << "x" << m_numTilesY << " = " << m_totalTiles << " tiles)" << std::endl;
}

void TileRasterizer::pass1_countTrianglesPerTile(const glm::mat4& viewProjection, size_t triangleCount) {
    std::vector<GLuint> zeros(m_totalTiles, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileCounterBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, m_totalTiles * sizeof(GLuint), zeros.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

size_t TileRasterizer::pass2_prefixSum() {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileOffsetBuffer);

    GLuint totalPairs = 0;
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 
                       m_totalTiles * sizeof(GLuint), 
                       sizeof(GLuint), 
                       &totalPairs);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Cap to prevent OOM / GPU crash
    const GLuint MAX_PAIRS = 64 * 1024 * 1024; // 64M pairs = 256 MB
    if (totalPairs > MAX_PAIRS) {
        std::cerr << "[WARNING] totalPairs (" << totalPairs << ") exceeds max, capping to " << MAX_PAIRS << std::endl;
        totalPairs = MAX_PAIRS;
    }

    m_totalTriangleTilePairs = totalPairs;

    if (totalPairs == 0) {
        return 0;
    }

    // Only reallocate if current capacity is insufficient
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

void TileRasterizer::pass3_binTriangles(size_t triangleCount) {
    std::vector<GLuint> zeros(m_totalTiles, 0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileWriteOffsetBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, m_totalTiles * sizeof(GLuint), zeros.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

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
