#include "COFFParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

// Load a mesh from an OFF or COFF file and populate outMesh.
//
// OFF (Object File Format) is a simple ASCII format:
//   Line 1: "OFF" or "COFF" (COFF = Colored OFF, has per-face RGB after vertex indices)
//   Line 2: <vertexCount> <faceCount> <edgeCount>
//   Next vertexCount lines: <x> <y> <z>
//   Next faceCount lines: 3 <i0> <i1> <i2> [<r> <g> <b> [<a>]]
//
// After geometry is parsed, smooth per-vertex normals are accumulated from face normals
// and normalized. The function returns false on any I/O or format error.
bool COFFParser::loadFromFile(const std::string& filePath, Mesh& outMesh) {
    auto startTime = std::chrono::high_resolution_clock::now();

    std::cout << "[INFO] Loading COFF file: " << filePath << std::endl;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Could not open file: " << filePath << std::endl;
        return false;
    }

    size_t vertexCount = 0;
    size_t faceCount = 0;

    if (!parseHeader(file, vertexCount, faceCount)) {
        file.close();
        return false;
    }

    std::cout << "[INFO] COFF Header: " << vertexCount << " vertices, " << faceCount << " faces" << std::endl;

    // Reserve exact capacity to avoid reallocation during parsing
    outMesh.vertices.reserve(vertexCount);
    std::cout << "[INFO] Pre-allocated memory for " << vertexCount << " vertices ("
              << (vertexCount * sizeof(Vertex) / 1024 / 1024) << " MB)" << std::endl;

    if (!parseVertices(file, vertexCount, outMesh.vertices)) {
        file.close();
        return false;
    }

    outMesh.faces.reserve(faceCount);
    std::cout << "[INFO] Pre-allocated memory for " << faceCount << " faces ("
              << (faceCount * sizeof(Face) / 1024 / 1024) << " MB)" << std::endl;

    if (!parseFaces(file, faceCount, outMesh.faces)) {
        file.close();
        return false;
    }

    file.close();

    // === SMOOTH NORMAL ACCUMULATION ===
    // Compute per-vertex normals by accumulating weighted face normals.
    // This gives smooth shading on curved surfaces without explicit normal data in the file.
    //
    // Algorithm:
    //   1. Zero all vertex normals.
    //   2. For each triangle, compute the face normal via cross product (v1-v0) × (v2-v0).
    //      The un-normalized length is proportional to face area, so larger faces contribute more
    //      (area-weighted normals — a simple and effective approximation for smooth meshes).
    //   3. Accumulate the face normal into each of the three vertices it touches.
    //   4. Normalize each vertex normal. Vertices shared by many faces get the average direction.
    //      Degenerate vertices (all contributing faces are zero-area) fall back to (0,1,0).

    // Step 1: zero all normals before accumulation
    for (auto& v : outMesh.vertices)
        v.normal = glm::vec3(0.0f);

    // Step 2+3: accumulate area-weighted face normals into each vertex
    for (const auto& f : outMesh.faces) {
        const glm::vec3& v0 = outMesh.vertices[f.indices.x].position;
        const glm::vec3& v1 = outMesh.vertices[f.indices.y].position;
        const glm::vec3& v2 = outMesh.vertices[f.indices.z].position;
        // Cross product gives a normal whose magnitude = 2 * face area
        glm::vec3 faceNormal = glm::cross(v1 - v0, v2 - v0);
        float len = glm::length(faceNormal);
        if (len > 1e-8f) faceNormal /= len; // Normalize to unit length for uniform weighting
        outMesh.vertices[f.indices.x].normal += faceNormal;
        outMesh.vertices[f.indices.y].normal += faceNormal;
        outMesh.vertices[f.indices.z].normal += faceNormal;
    }

    // Step 4: normalize accumulated normals; fall back to +Y for degenerate vertices
    for (auto& v : outMesh.vertices) {
        float len = glm::length(v.normal);
        if (len > 1e-8f) v.normal /= len;
        else             v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Degenerate vertex fallback
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "[INFO] Successfully loaded mesh in " << duration.count() << " ms" << std::endl;
    std::cout << "[INFO] Total memory: "
              << ((outMesh.getVertexBufferSize() + outMesh.getFaceBufferSize()) / 1024 / 1024)
              << " MB" << std::endl;

    return true;
}

// Parse the first two lines of an OFF/COFF file to determine vertex and face counts.
// Returns false if the magic string is not "OFF" or "COFF", or if the counts can't be read.
// The trailing newline after the counts is consumed so parseVertices starts at the first vertex.
bool COFFParser::parseHeader(std::ifstream& file, size_t& vertexCount, size_t& faceCount) {
    std::string header;
    std::getline(file, header);

    // Accept both "OFF" (no colors) and "COFF" (per-face colors optional)
    if (header != "COFF" && header != "OFF") {
        std::cerr << "[ERROR] Invalid file format. Expected 'COFF' or 'OFF', got: " << header << std::endl;
        return false;
    }

    // Edge count is part of the format but unused — stored in a dummy variable
    size_t edgeCount;
    file >> vertexCount >> faceCount >> edgeCount;

    if (file.fail()) {
        std::cerr << "[ERROR] Failed to parse header counts" << std::endl;
        return false;
    }

    if (vertexCount == 0 || faceCount == 0) {
        std::cerr << "[ERROR] Invalid mesh: vertex or face count is zero" << std::endl;
        return false;
    }

    // Consume the rest of the count line so the stream is positioned at the first vertex line
    std::string dummy;
    std::getline(file, dummy);

    return true;
}

// Parse `vertexCount` vertex lines, each containing "x y z".
// Padding fields (_pad0, _pad1) are zeroed to satisfy the std430 Vertex struct layout.
// Normals are left as zero — they will be computed by smooth normal accumulation after parseFaces().
// Logs progress every 500k vertices for large files.
bool COFFParser::parseVertices(std::ifstream& file, size_t vertexCount, std::vector<Vertex>& vertices) {
    std::cout << "[INFO] Parsing vertices..." << std::endl;

    for (size_t i = 0; i < vertexCount; ++i) {
        Vertex vertex;
        file >> vertex.position.x >> vertex.position.y >> vertex.position.z;

        if (file.fail()) {
            std::cerr << "[ERROR] Failed to parse vertex at index " << i << std::endl;
            return false;
        }

        // Zero padding and normal — normals will be filled in by smooth normal pass
        vertex._pad0   = 0.0f;
        vertex.normal  = glm::vec3(0.0f);
        vertex._pad1   = 0.0f;
        vertices.push_back(vertex);

        if (i % 500000 == 0 && i > 0) {
            std::cout << "[INFO] Parsed " << i << " / " << vertexCount << " vertices..." << std::endl;
        }
    }

    std::cout << "[INFO] All " << vertexCount << " vertices parsed successfully" << std::endl;
    return true;
}

// Parse `faceCount` face lines from a COFF/OFF file.
// Each line has the form: "3 i0 i1 i2 [r g b [a]]"
//   - Non-triangular faces (numVertices != 3) are skipped with a warning.
//   - Color: COFF files may or may not include per-face colors. The parser attempts
//     to read 3 floats after the indices. Values > 1 are interpreted as 0-255 integers
//     and normalized to [0,1]. If no color is present, a default grey (0.8, 0.8, 0.8) is used.
//   - Alpha: if a 4th color value is present it is consumed but not stored.
//
// Remaining characters on each line are consumed via getline to keep the stream
// positioned correctly for the next face regardless of optional trailing data.
bool COFFParser::parseFaces(std::ifstream& file, size_t faceCount, std::vector<Face>& faces) {
    std::cout << "[INFO] Parsing faces..." << std::endl;

    for (size_t i = 0; i < faceCount; ++i) {
        int numVertices;
        file >> numVertices;

        if (file.fail()) {
            std::cerr << "[ERROR] Failed to parse face count at index " << i << std::endl;
            return false;
        }

        // This renderer only supports triangles; skip quads/polygons with a warning
        if (numVertices != 3) {
            std::cerr << "[WARNING] Non-triangle face detected at index " << i
                      << " (vertices: " << numVertices << "). Skipping." << std::endl;
            std::string line;
            std::getline(file, line);
            continue;
        }

        Face face;
        file >> face.indices.x >> face.indices.y >> face.indices.z;

        if (file.fail()) {
            std::cerr << "[ERROR] Failed to parse face indices at index " << i << std::endl;
            return false;
        }

        // Remember stream position before attempting to read color.
        // If the read fails (plain OFF with no color), we seek back and use the default.
        std::streampos posBeforeColor = file.tellg();

        if (file >> face.color.r >> face.color.g >> face.color.b) {
            // Detect 8-bit integer encoding: any value > 1.0 means 0-255 range
            if (face.color.r > 1.0f || face.color.g > 1.0f || face.color.b > 1.0f) {
                face.color.r /= 255.0f;
                face.color.g /= 255.0f;
                face.color.b /= 255.0f;
            }

            // Attempt to read optional alpha channel; discard it (renderer uses opaque geometry)
            float alpha;
            if (file >> alpha) {
                // Alpha present but unused
            } else {
                file.clear(); // clear failbit so subsequent reads proceed normally
            }
        } else {
            // No color data — reset stream to before the failed read, use default grey
            file.clear();
            file.seekg(posBeforeColor);

            face.color = glm::vec3(0.8f, 0.8f, 0.8f); // Neutral grey fallback
        }

        // Consume any remaining data on this line (e.g., extra COFF fields or whitespace)
        std::string remainingLine;
        std::getline(file, remainingLine);

        // Zero padding fields to satisfy the std430 Face struct layout
        face._pad0    = 0;
        face.padding  = 0.0f;
        faces.push_back(face);

        if (i % 200000 == 0 && i > 0) {
            std::cout << "[INFO] Parsed " << i << " / " << faceCount << " faces..." << std::endl;
        }
    }

    std::cout << "[INFO] All " << faceCount << " faces parsed successfully" << std::endl;
    return true;
}
