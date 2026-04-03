#include "COFFParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>

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

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "[INFO] Successfully loaded mesh in " << duration.count() << " ms" << std::endl;
    std::cout << "[INFO] Total memory: " 
              << ((outMesh.getVertexBufferSize() + outMesh.getFaceBufferSize()) / 1024 / 1024) 
              << " MB" << std::endl;

    return true;
}

bool COFFParser::parseHeader(std::ifstream& file, size_t& vertexCount, size_t& faceCount) {
    std::string header;
    std::getline(file, header);
    
    if (header != "COFF" && header != "OFF") {
        std::cerr << "[ERROR] Invalid file format. Expected 'COFF' or 'OFF', got: " << header << std::endl;
        return false;
    }

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

    std::string dummy;
    std::getline(file, dummy);
    
    return true;
}

bool COFFParser::parseVertices(std::ifstream& file, size_t vertexCount, std::vector<Vertex>& vertices) {
    std::cout << "[INFO] Parsing vertices..." << std::endl;
    
    for (size_t i = 0; i < vertexCount; ++i) {
        Vertex vertex;
        file >> vertex.position.x >> vertex.position.y >> vertex.position.z;
        
        if (file.fail()) {
            std::cerr << "[ERROR] Failed to parse vertex at index " << i << std::endl;
            return false;
        }

        vertex._pad0 = 0.0f;
        vertex.padding = glm::vec3(0.0f);
        vertex._pad1 = 0.0f;
        vertices.push_back(vertex);

        if (i % 500000 == 0 && i > 0) {
            std::cout << "[INFO] Parsed " << i << " / " << vertexCount << " vertices..." << std::endl;
        }
    }

    std::cout << "[INFO] All " << vertexCount << " vertices parsed successfully" << std::endl;
    return true;
}

bool COFFParser::parseFaces(std::ifstream& file, size_t faceCount, std::vector<Face>& faces) {
    std::cout << "[INFO] Parsing faces..." << std::endl;
    
    for (size_t i = 0; i < faceCount; ++i) {
        int numVertices;
        file >> numVertices;
        
        if (file.fail()) {
            std::cerr << "[ERROR] Failed to parse face count at index " << i << std::endl;
            return false;
        }

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

        std::streampos posBeforeColor = file.tellg();

        if (file >> face.color.r >> face.color.g >> face.color.b) {
            if (face.color.r > 1.0f || face.color.g > 1.0f || face.color.b > 1.0f) {
                face.color.r /= 255.0f;
                face.color.g /= 255.0f;
                face.color.b /= 255.0f;
            }

            float alpha;
            if (file >> alpha) {
                if (alpha > 1.0f) {
                }
            } else {
                file.clear();
            }
        } else {
            file.clear();
            file.seekg(posBeforeColor);

            face.color = glm::vec3(0.8f, 0.8f, 0.8f);
        }

        std::string remainingLine;
        std::getline(file, remainingLine);

        face._pad0 = 0;
        face.padding = 0.0f;
        faces.push_back(face);

        if (i % 200000 == 0 && i > 0) {
            std::cout << "[INFO] Parsed " << i << " / " << faceCount << " faces..." << std::endl;
        }
    }

    std::cout << "[INFO] All " << faceCount << " faces parsed successfully" << std::endl;
    return true;
}
