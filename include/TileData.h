#ifndef TILE_DATA_H
#define TILE_DATA_H

#include <glm/glm.hpp>

// ProjectedTriangle — result of pass1 for a single triangle.
// Stores the 2D screen-space vertices, per-vertex NDC depths, screen-space AABB,
// and the original face index needed by pass4 to look up color/normals.
//
// std430 layout — must exactly match the GLSL struct in pass1/pass3/pass4 shaders.
// Total size = 48 bytes (verified by field breakdown below):
//   v0,v1,v2 (3×8=24) + depth0/1/2/_pad0 (4×4=16) + aabbMin/Max (2×8=16) + originalFaceIndex+padding[3] (4×4=16) = 72 bytes
//   Note: actual size depends on compiler; padding[3] ensures correct alignment.
struct ProjectedTriangle {
    glm::vec2 v0;   // Screen-space position of vertex 0 (pixels, origin bottom-left)
    glm::vec2 v1;   // Screen-space position of vertex 1
    glm::vec2 v2;   // Screen-space position of vertex 2

    float depth0;   // NDC depth of vertex 0 (used for barycentric depth interpolation)
    float depth1;   // NDC depth of vertex 1
    float depth2;   // NDC depth of vertex 2
    float _pad0;    // Padding: ivec2 has 8-byte alignment; 3 floats = 12 bytes, +4 aligns to 16

    glm::ivec2 aabbMin; // Screen-space AABB minimum corner (tile overlap test in pass1/pass3)
    glm::ivec2 aabbMax; // Screen-space AABB maximum corner

    glm::uint originalFaceIndex; // Index into the Face SSBO — used in pass4 to fetch color/indices
    glm::uint padding[3];        // Padding to keep struct size a multiple of 16 bytes
};

// TileInfo — compile-time tile grid constants and helper for tile count calculation.
// TILE_SIZE must match the local_size_x/y in all tiled compute shaders (pass4).
// Changing it requires updating workgroup sizes in: pass4_rasterize_tiled.comp,
// pass1_count.comp, clear_depth.comp, and the dispatch calculations in Renderer.cpp.
struct TileInfo {
    static constexpr int TILE_SIZE = 16; // Each tile covers a 16×16 pixel region

    int numTilesX;   // Number of tiles along the X axis (ceil(screenWidth  / TILE_SIZE))
    int numTilesY;   // Number of tiles along the Y axis (ceil(screenHeight / TILE_SIZE))
    int totalTiles;  // numTilesX * numTilesY — total number of tiles for this viewport

    // Returns total tile count for a given screen resolution.
    // Uses ceiling division so partial tiles at screen edges are included.
    static int calculateTileCount(int screenWidth, int screenHeight) {
        int tilesX = (screenWidth  + TILE_SIZE - 1) / TILE_SIZE;
        int tilesY = (screenHeight + TILE_SIZE - 1) / TILE_SIZE;
        return tilesX * tilesY;
    }
};

#endif // TILE_DATA_H
