#ifndef TILE_DATA_H
#define TILE_DATA_H

#include <glm/glm.hpp>

struct ProjectedTriangle {
    glm::vec2 v0;
    glm::vec2 v1;
    glm::vec2 v2;

    float depth0;
    float depth1;
    float depth2;
    float _pad0;           // std430: ivec2 has 8-byte alignment, need padding after 3 floats

    glm::ivec2 aabbMin;
    glm::ivec2 aabbMax;

    glm::uint originalFaceIndex;
    glm::uint padding[3];
};

struct TileInfo {
    static constexpr int TILE_SIZE = 16;
    
    int numTilesX;
    int numTilesY;
    int totalTiles;
    
    static int calculateTileCount(int screenWidth, int screenHeight) {
        int tilesX = (screenWidth + TILE_SIZE - 1) / TILE_SIZE;
        int tilesY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;
        return tilesX * tilesY;
    }
};

#endif // TILE_DATA_H
