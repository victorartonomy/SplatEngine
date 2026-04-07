#ifndef TEXTURE_MANAGER_H
#define TEXTURE_MANAGER_H

#include <glad/glad.h>
#include <string>
#include <vector>

// TextureManager — loads image files and maintains a GL_TEXTURE_2D_ARRAY on the GPU.
//
// All textures are scaled to MAX_TEXTURE_SIZE × MAX_TEXTURE_SIZE RGBA8 before upload so
// every layer in the GL_TEXTURE_2D_ARRAY has the same dimensions (a hard OpenGL requirement).
//
// Typical usage:
//   1. addTexture(path)  — returns the layer index, store as GPUMaterial::textureID
//   2. upload()          — rebuild the texture array on the GPU after adding textures
//   3. bind(unit)        — before each pass4 dispatch; pair with setInt("textureAtlas", unit)
//
// GPUMaterial::textureID == -1 is the "no texture" sentinel handled entirely in the shader;
// the TextureManager itself has no special knowledge of that value.
class TextureManager {
public:
    // All textures are resampled to this square size. 512 is a good balance of quality vs VRAM.
    static constexpr int MAX_TEXTURE_SIZE = 512;

    TextureManager()  = default;
    ~TextureManager() = default;

    TextureManager(const TextureManager&)            = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // Load an image file (PNG/JPG/TGA/BMP) and append it as a new layer.
    // The image is force-converted to RGBA and scaled to MAX_TEXTURE_SIZE × MAX_TEXTURE_SIZE.
    // Returns the layer index (use as GPUMaterial::textureID), or -1 on load failure.
    int addTexture(const std::string& filePath);

    // Returns the number of texture layers currently loaded on the CPU side.
    int getTextureCount() const { return static_cast<int>(m_layers.size()); }

    // Returns the file path for a given layer index (used for UI display). "" if out of range.
    const std::string& getTexturePath(int index) const;

    // Upload all CPU layers to a GL_TEXTURE_2D_ARRAY on the GPU.
    // Creates or recreates the texture object; generates mipmaps.
    // If no layers exist, creates a 1×1 white dummy so the sampler is always valid.
    void upload();

    // Bind the texture array to the specified OpenGL texture unit.
    // Caller must then set the "textureAtlas" sampler uniform to the same unit index.
    void bind(GLuint unit) const;

    // Release the GL texture object. Call before destroying the OpenGL context.
    void shutdown();

private:
    GLuint m_texture = 0;  // GL_TEXTURE_2D_ARRAY handle (0 = not yet created)

    // CPU-side image data: one RGBA8 buffer per layer, each MAX_TEXTURE_SIZE×MAX_TEXTURE_SIZE×4 bytes
    std::vector<std::vector<unsigned char>> m_layers;
    std::vector<std::string>                m_paths;   // parallel file paths for UI display
};

#endif // TEXTURE_MANAGER_H
