#ifndef RENDERER_H
#define RENDERER_H

#include <glad/glad.h>
#include <memory>

#include "Camera.h"
#include "ComputeShader.h"
#include "GPUBuffer.h"
#include "LightManager.h"
#include "Mesh.h"
#include "Scene.h"
#include "TileRasterizer.h"

class AssetManager;

class Renderer {
public:
    Renderer()  = default;
    ~Renderer() = default;

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize all GPU resources. Must be called after the OpenGL context exists.
    bool initialize(int viewportWidth, int viewportHeight);

    // Release all GPU resources. Must be called before the OpenGL context is destroyed.
    void shutdown();

    // Rebuild the merged GPU buffers from the current scene state.
    // Call whenever the scene is mutated (entity added/removed, transform changed).
    void submitScene(Scene& scene, const AssetManager& am);

    // Execute the full 5-phase compute pipeline, writing to the internal output texture.
    void render(Scene& scene, const Camera& camera, const AssetManager& am);

    // Recreate viewport-sized render targets when the panel is resized.
    void resize(int newWidth, int newHeight);

    GLuint getOutputTexture()  const { return m_outputTexture;  }
    int    getViewportWidth()  const { return m_viewportWidth;  }
    int    getViewportHeight() const { return m_viewportHeight; }

    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool getDebugMode()       const { return m_debugMode;    }

    LightManager& getLightManager() { return m_lightManager; }

private:
    GLuint createOutputTexture(int width, int height);
    GLuint createDepthBuffer(int width, int height);

    // Compute shaders
    std::unique_ptr<ComputeShader> m_clearDepthShader;
    std::unique_ptr<ComputeShader> m_clearTilesShader;
    std::unique_ptr<ComputeShader> m_pass1Shader;
    std::unique_ptr<ComputeShader> m_pass2Shader;
    std::unique_ptr<ComputeShader> m_pass3Shader;
    std::unique_ptr<ComputeShader> m_pass4Shader;
    std::unique_ptr<ComputeShader> m_debugTilesShader;

    // Viewport render targets
    GLuint m_outputTexture  = 0;
    GLuint m_depthBuffer    = 0;
    int    m_viewportWidth  = 256;
    int    m_viewportHeight = 256;

    // Tile pipeline
    std::unique_ptr<TileRasterizer> m_tileRasterizer;

    // Merged scene geometry (CPU-transformed, uploaded once per submitScene call)
    Mesh      m_mergedMesh;
    GPUBuffer m_mergedGpuBuffer;

    LightManager m_lightManager;

    bool m_sceneDirty = true;   // safety net: auto-submit on first render if missed
    bool m_debugMode  = false;
};

#endif // RENDERER_H
