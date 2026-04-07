#include "TextureManager.h"
#include <iostream>
#include <algorithm>

// STB_IMAGE_IMPLEMENTATION and STB_IMAGE_RESIZE_IMPLEMENTATION must be defined in exactly
// ONE .cpp file across the entire project. TextureManager.cpp is that file.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

int TextureManager::addTexture(const std::string& filePath) {
    // Load with stb_image, forcing 4-channel RGBA output regardless of source format.
    int srcW, srcH, srcChannels;
    unsigned char* srcData = stbi_load(filePath.c_str(), &srcW, &srcH, &srcChannels, 4);
    if (!srcData) {
        std::cerr << "[TextureManager] Failed to load '" << filePath
                  << "': " << stbi_failure_reason() << std::endl;
        return -1;
    }

    // Resize to MAX_TEXTURE_SIZE × MAX_TEXTURE_SIZE so all array layers are the same size.
    // stride = 0 tells stb_image_resize2 that rows are tightly packed (no padding).
    std::vector<unsigned char> resized(MAX_TEXTURE_SIZE * MAX_TEXTURE_SIZE * 4);
    stbir_resize_uint8_linear(
        srcData,        srcW,              srcH,              0,
        resized.data(), MAX_TEXTURE_SIZE,  MAX_TEXTURE_SIZE,  0,
        STBIR_RGBA);

    stbi_image_free(srcData);

    int layerIndex = static_cast<int>(m_layers.size());
    m_layers.push_back(std::move(resized));
    m_paths.push_back(filePath);

    std::cout << "[TextureManager] Loaded layer " << layerIndex << ": " << filePath << std::endl;
    return layerIndex;
}

const std::string& TextureManager::getTexturePath(int index) const {
    static const std::string empty;
    if (index < 0 || index >= static_cast<int>(m_paths.size()))
        return empty;
    return m_paths[index];
}

void TextureManager::upload() {
    // Recreate the texture object from scratch each time. For an editor workflow with rare
    // texture additions, a full recreate is simple and correct.
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);

    if (m_layers.empty()) {
        // No user textures loaded. Create a 1×1 white dummy so the sampler uniform is
        // always bound to a complete texture object. Shader skips sampling when textureID<0.
        unsigned char white[4] = { 255, 255, 255, 255 };
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                     1, 1, 1,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    } else {
        // Allocate storage for all layers at once (null data = reserve only).
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                     MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE,
                     static_cast<GLsizei>(m_layers.size()),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        for (int i = 0; i < static_cast<int>(m_layers.size()); ++i) {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                            0, 0, i,
                            MAX_TEXTURE_SIZE, MAX_TEXTURE_SIZE, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, m_layers[i].data());
        }

        // Mipmaps improve quality at oblique angles and far distances.
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }

    // Trilinear min, bilinear mag, repeat wrap.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,     GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    std::cout << "[TextureManager] Uploaded " << std::max(1, (int)m_layers.size())
              << " layer(s) to GL_TEXTURE_2D_ARRAY" << std::endl;
}

void TextureManager::bind(GLuint unit) const {
    if (m_texture != 0) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_texture);
    }
}

void TextureManager::shutdown() {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_layers.clear();
    m_paths.clear();
}
