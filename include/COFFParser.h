#ifndef COFF_PARSER_H
#define COFF_PARSER_H

#include "Mesh.h"
#include <string>

class COFFParser {
public:
    static bool loadFromFile(const std::string& filePath, Mesh& outMesh);

private:
    static bool parseHeader(std::ifstream& file, size_t& vertexCount, size_t& faceCount);
    static bool parseVertices(std::ifstream& file, size_t vertexCount, std::vector<Vertex>& vertices);
    static bool parseFaces(std::ifstream& file, size_t faceCount, std::vector<Face>& faces);
};

#endif // COFF_PARSER_H
