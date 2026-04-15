#ifndef RENDERER_H
#define RENDERER_H

#include <glad/glad.h>
#include <memory>

#include "Camera.h"
#include "ComputeShader.h"
#include "GPUBuffer.h"
#include "LightManager.h"
#include "MaterialManager.h"
#include "Mesh.h"
#include "TextureManager.h"
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
    // Returns the texture ImGui should sample. When FXAA is enabled this is the
    // post-processed presentation image; otherwise it's the raw pass5 output.
    GLuint getDisplayTexture() const {
        return m_fxaaEnabled ? m_presentationTexture : m_outputTexture;
    }
    int    getViewportWidth()  const { return m_viewportWidth;  }
    int    getViewportHeight() const { return m_viewportHeight; }

    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool getDebugMode()       const { return m_debugMode;    }

    // --- FXAA (pass6_fxaa.comp) toggles ---
    // When disabled the pass6 dispatch is skipped entirely and getDisplayTexture()
    // falls back to the raw output, so the off state has zero GPU cost.
    bool  getFXAAEnabled()          const { return m_fxaaEnabled;          }
    void  setFXAAEnabled(bool e)          { m_fxaaEnabled = e;             }
    float getFXAAQualitySubpix()    const { return m_fxaaQualitySubpix;    }
    void  setFXAAQualitySubpix(float v)   { m_fxaaQualitySubpix = v;       }
    float getFXAAEdgeThreshold()    const { return m_fxaaEdgeThreshold;    }
    void  setFXAAEdgeThreshold(float v)   { m_fxaaEdgeThreshold = v;       }
    float getFXAAEdgeThresholdMin() const { return m_fxaaEdgeThresholdMin; }
    void  setFXAAEdgeThresholdMin(float v){ m_fxaaEdgeThresholdMin = v;    }

    LightManager&    getLightManager()    { return m_lightManager;    }
    MaterialManager& getMaterialManager() { return m_materialManager; }
    TextureManager&  getTextureManager()  { return m_textureManager;  }

private:
    GLuint createOutputTexture(int width, int height);
    GLuint createDepthBuffer(int width, int height);
    // Create the R32UI shadow map texture used by the shadow pass and sampled in pass4.
    GLuint createShadowMap(int size);
    // Allocate (or reallocate) all 5 OIT accumulation SSBOs to hold width×height uint32 values.
    // Called from initialize() and resize() whenever the viewport dimensions change.
    void   createOITBuffers(int width, int height);

    // Compute shaders
    std::unique_ptr<ComputeShader> m_clearDepthShader;
    std::unique_ptr<ComputeShader> m_clearTilesShader;
    std::unique_ptr<ComputeShader> m_pass1Shader;
    std::unique_ptr<ComputeShader> m_pass2Shader;
    std::unique_ptr<ComputeShader> m_pass3Shader;
    std::unique_ptr<ComputeShader> m_pass4Shader;
    // OIT composite pass: reads OIT accumulation SSBOs written by pass4 and blends
    // weighted transparent geometry over the opaque output image.
    std::unique_ptr<ComputeShader> m_pass5OITShader;
    // FXAA post-process: samples m_outputTexture, writes m_presentationTexture.
    std::unique_ptr<ComputeShader> m_pass6FXAAShader;
    std::unique_ptr<ComputeShader> m_debugTilesShader;
    // Shadow mapping shaders — clear the shadow map then rasterise scene depth from the light.
    std::unique_ptr<ComputeShader> m_clearShadowShader;
    std::unique_ptr<ComputeShader> m_shadowPassShader;

    // Viewport render targets
    GLuint m_outputTexture  = 0;
    GLuint m_depthBuffer    = 0;
    // FXAA destination. Separate from m_outputTexture so we never read+write the same
    // texture through a sampler+image binding simultaneously. Shares format/filters/wrap
    // with m_outputTexture (allocated through createOutputTexture()).
    GLuint m_presentationTexture = 0;
    int    m_viewportWidth  = 256;
    int    m_viewportHeight = 256;

    // Shadow map (R32UI, fixed-resolution, independent of viewport size).
    // 2048×2048 gives reasonable quality without huge memory cost (~16 MB).
    GLuint m_shadowMapTexture = 0;
    int    m_shadowMapSize    = 2048;

    // OIT (Weighted Blended Order-Independent Transparency) accumulation SSBOs.
    // Each buffer stores one float per pixel, packed as a uint32 via floatBitsToUint.
    // pass4 writes thread-locally accumulated WBOIT data for each pixel at the end of main().
    // pass5 reads these and composites transparent fragments over the opaque output image.
    //
    // Buffer contents per pixel (SSBO bindings 9-13):
    //   oitAccumR/G/B: sum(channel * alpha * weight) — weighted color accumulation
    //   oitAccumA:     sum(alpha * weight)             — weighted alpha accumulation
    //   oitReveal:     product(1 - alpha_i)            — transmittance (1=fully unoccluded)
    //
    // These are resized in Renderer::resize() alongside the depth buffer.
    GLuint m_oitAccumR = 0;  // SSBO binding 9
    GLuint m_oitAccumG = 0;  // SSBO binding 10
    GLuint m_oitAccumB = 0;  // SSBO binding 11
    GLuint m_oitAccumA = 0;  // SSBO binding 12
    GLuint m_oitReveal = 0;  // SSBO binding 13

    // Tile pipeline
    std::unique_ptr<TileRasterizer> m_tileRasterizer;

    // Merged scene geometry (CPU-transformed, uploaded once per submitScene call)
    Mesh      m_mergedMesh;
    GPUBuffer m_mergedGpuBuffer;

    LightManager    m_lightManager;
    MaterialManager m_materialManager;
    TextureManager  m_textureManager;

    bool m_sceneDirty = true;   // safety net: auto-submit on first render if missed
    bool m_debugMode  = false;

    // FXAA state. Defaults match FXAA 3.11 "PC Quality" presets.
    bool  m_fxaaEnabled          = true;
    float m_fxaaQualitySubpix    = 0.75f;   // [0, 1] subpixel blend strength
    float m_fxaaEdgeThreshold    = 0.166f;  // local-contrast threshold
    float m_fxaaEdgeThresholdMin = 0.0833f; // absolute dark cutoff
};

#endif // RENDERER_H
