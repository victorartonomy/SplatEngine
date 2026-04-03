#ifndef TILE_RASTERIZER_H
#define TILE_RASTERIZER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "TileData.h"

class TileRasterizer {
public:
    TileRasterizer(int screenWidth, int screenHeight);
    ~TileRasterizer();

    TileRasterizer(const TileRasterizer&) = delete;
    TileRasterizer& operator=(const TileRasterizer&) = delete;

    void initialize(size_t maxTriangles);
    void resize(int newWidth, int newHeight, size_t maxTriangles);
    void pass1_countTrianglesPerTile(const glm::mat4& viewProjection, size_t triangleCount);
    size_t pass2_prefixSum();
    void pass3_binTriangles(size_t triangleCount);

    GLuint getProjectedTriangleBufferID() const { return m_projectedTriangleBuffer; }
    GLuint getTileCounterBufferID() const { return m_tileCounterBuffer; }
    GLuint getTileOffsetBufferID() const { return m_tileOffsetBuffer; }
    GLuint getTriangleListBufferID() const { return m_triangleListBuffer; }
    GLuint getBlockSumBufferID() const { return m_blockSumBuffer; }
    GLuint getTileWriteOffsetBufferID() const { return m_tileWriteOffsetBuffer; }

    int getNumTilesX() const { return m_numTilesX; }
    int getNumTilesY() const { return m_numTilesY; }
    int getTotalTiles() const { return m_totalTiles; }
    size_t getTotalTriangleTilePairs() const { return m_totalTriangleTilePairs; }

private:
    int m_screenWidth;
    int m_screenHeight;
    int m_numTilesX;
    int m_numTilesY;
    int m_totalTiles;
    size_t m_totalTriangleTilePairs;

    GLuint m_projectedTriangleBuffer;
    GLuint m_tileCounterBuffer;
    GLuint m_tileOffsetBuffer;
    GLuint m_triangleListBuffer;
    GLuint m_blockSumBuffer;
    GLuint m_tileWriteOffsetBuffer;
    size_t m_triangleListCapacity;

    void cleanup();
};

#endif // TILE_RASTERIZER_H
