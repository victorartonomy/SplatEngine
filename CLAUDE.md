# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (Windows/MSVC)
cmake -B build -G "Visual Studio 18 2026" -A x64

# Build
cmake --build build --config Release

# Run
./build/Release/Engine.exe

# Linux alternative
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Engine
```

CMake presets are configured for Visual Studio 18 2026. Dependencies (GLFW, GLM, Dear ImGui docking branch, Native File Dialog Extended, GLAD) are all auto-fetched via FetchContent -- no manual installs needed.

Shaders and models are auto-copied to the build directory via post-build commands. After editing a shader, you must rebuild (or manually copy) for changes to take effect.

There are no tests or linting configured.

## Architecture

This is a **Triangle Splatting** renderer -- a compute-shader-only rendering engine. There is NO traditional vertex/fragment graphics pipeline for geometry. All projection, binning, rasterization, and depth testing run as compute shader dispatches. The screen is divided into 16x16 pixel tiles for efficient per-pixel work.

### Rendering Pipeline (orchestrated in `Renderer.cpp`)

| Phase | Shader | Purpose |
|-------|--------|---------|
| 0: Clear | `clear_depth.comp`, `clear_tiles.comp` | Reset depth buffer (R32UI) to 0xFFFFFFFF and tile counters to 0 |
| 1: Project & Count | `pass1_count.comp` | Project triangles to screen space, compute AABBs, atomically count triangles per tile |
| 1.5: Prefix Sum | `pass2_prefix_sum.comp` (3 dispatches) | Blelloch exclusive scan on tile counters; CPU readback of total triangle-tile pairs |
| 2: Bin | `pass3_bin.comp` | Scatter triangle IDs into per-tile lists using prefix-sum offsets |
| 3: Rasterize | `pass4_rasterize_tiled.comp` | Per-pixel edge-function tests, barycentric depth/normal/position interpolation, imageAtomicMin depth, Blinn-Phong or PBR shading with multi-light support |
| 4: Display | ImGui viewport | Sample output texture and present |

### Lighting System

The rasterizer (`pass4_rasterize_tiled.comp`) supports a full lighting pipeline:

- **Light types:** Directional, Point, Spot (with inner/outer cone falloff)
- **Shading models:** Blinn-Phong (specular + diffuse) and PBR metallic-roughness (GGX/Smith/Schlick Cook-Torrance), switchable at runtime via UI
- **Light data** is uploaded as a `GPULight` SSBO at binding 7. `LightManager` rebuilds this buffer each frame from scene entities.
- If no light entities exist, a default directional light is used as fallback.
- Uniforms passed to pass4: `numLights`, `cameraPos`, `shadingModel` (0 = Blinn-Phong, 1 = PBR)

### Event System

Subsystems communicate via a synchronous publish/subscribe event bus (`EventBus.h/cpp`):

- **WindowResizeEvent** -- viewport resize triggers `Renderer::resize()`
- **AssetLoadedEvent** -- fired after GPU upload completes (sync and async paths)
- **ActionEvent** -- published on input state change (rising/falling edge), used for quit action

### Key Components

- **Application** (`Application.h/cpp`) -- Render loop, ImGui editor UI (Viewport, Scene Hierarchy, Asset Manager, Camera Settings, Lighting panel), scene management, camera controls
- **Renderer** (`Renderer.h/cpp`) -- Orchestrates the full 5-phase compute pipeline, manages shaders/GPU resources, owns the merged scene mesh and LightManager
- **LightManager** (`LightManager.h/cpp`) -- Collects light entities, builds `GPULight` SSBO, manages shading model selection
- **Light** (`Light.h`) -- `Light` struct (CPU), `GPULight` struct (64-byte std430), `LightType`/`ShadingModel` enums, `toGPULight()` converter
- **Entity** (`Entity.h`) -- Scene entity with transform, optional mesh asset, optional light component (`std::optional<Light>`)
- **EventBus** (`EventBus.h/cpp`) -- Type-safe synchronous pub/sub using `std::type_index`; global `bus()` singleton
- **Events** (`Events.h`) -- `WindowResizeEvent`, `AssetLoadedEvent`, `ActionEvent` structs
- **InputManager** (`InputManager.h/cpp`) -- Named action/axis polling, rebindable GLFW key mappings; publishes `ActionEvent` on state changes
- **Camera** (`Camera.h/cpp`, `CameraState.h`) -- FPS 6DOF camera with quaternion orientation, orbit mode, bookmarks
- **COFFParser** (`COFFParser.h/cpp`) -- Loads OFF/COFF mesh files with optional per-face RGB colors; computes smooth per-vertex normals after parsing
- **GPUBuffer** (`GPUBuffer.h/cpp`) -- Manages Vertex and Face SSBOs
- **TileRasterizer** (`TileRasterizer.h/cpp`, `TileData.h`) -- Allocates and manages 6 SSBOs for the tiling pipeline, handles prefix sum CPU readback
- **ComputeShader** (`ComputeShader.h/cpp`) -- Loads/compiles .comp files, dispatches with memory barriers; uniform API: `setInt`, `setUInt`, `setFloat`, `setVec3`, `setIVec2`, `setMat4`
- **AssetManager** (`AssetManager.h/cpp`) -- Centralized mesh loading, caching, reference counting, async support; publishes `AssetLoadedEvent` on GPU upload completion
- **Scene** (`Scene.h/cpp`) -- Entity storage, scene bounds calculation
- **Transform** (`Transform.h`) -- Position/rotation/scale with `getModelMatrix()`

## SSBO Binding Map

### pass1_count.comp
| Binding | Buffer |
|---------|--------|
| 0 | VertexBuffer |
| 1 | FaceBuffer |
| 2 | ProjectedTriangleBuffer |
| 3 | TileCounterBuffer |

### pass2_prefix_sum.comp
| Binding | Buffer |
|---------|--------|
| 0 | TileCounterBuffer |
| 1 | TileOffsetBuffer |
| 2 | BlockSumBuffer |

### pass3_bin.comp
| Binding | Buffer |
|---------|--------|
| 0 | ProjectedTriangleBuffer |
| 1 | TileCounterBuffer |
| 2 | TileOffsetBuffer |
| 3 | TriangleListBuffer |
| 4 | TileWriteOffsetBuffer |

### pass4_rasterize_tiled.comp
| Binding | Type | Buffer |
|---------|------|--------|
| 0 | image2D | outputImage |
| 1 | uimage2D | depthBuffer |
| 2 | SSBO | VertexBuffer |
| 3 | SSBO | FaceBuffer |
| 4 | SSBO | ProjectedTriangleBuffer |
| 5 | SSBO | TileOffsetBuffer |
| 6 | SSBO | TriangleListBuffer |
| 7 | SSBO | LightBuffer |

## Critical Constraints

- **std430 padding is load-bearing.** C++ structs in `Mesh.h`, `TileData.h`, and `Light.h` (`GPULight`) have explicit padding fields to match GLSL 16-byte alignment. Removing or reordering padding breaks GPU/CPU data communication. `GPULight` is validated with `static_assert(sizeof(GPULight) == 64)`.
- **SSBO binding points** must stay unique and consistent between C++ code and .comp shader files. Bindings 0-7 are in use in pass4. When adding new SSBOs, use binding 8+.
- **Normal transform:** `Renderer::submitScene()` transforms normals with `transpose(inverse(mat3(model)))`. Do not use the model matrix directly for normals.
- **Memory barriers** (`glMemoryBarrier`) are required between every compute pass. Use `GL_SHADER_STORAGE_BARRIER_BIT` and `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`.
- **Coordinate systems:** World space is right-handed. Screen space (0,0) is bottom-left (standard OpenGL).
- **OpenGL 4.3 Core** is the minimum required version.

## Adding a New Shader

1. Add `.comp` file to `shaders/`
2. Instantiate `ComputeShader` in `Renderer.cpp`
3. Dispatch in the appropriate phase of the render loop in `Renderer::render()`
4. Shaders auto-copy to build dir on rebuild

## Where to Edit

- Camera behavior: `Camera.cpp`
- Key bindings / input actions: `InputManager.cpp` (`initialize()`)
- Rasterization logic / shading / lighting: `pass4_rasterize_tiled.comp`
- Light types and GPU struct: `Light.h` (must keep `GPULight` at 64 bytes and update GLSL struct to match)
- Light buffer management: `LightManager.cpp`
- UI/editor features: search for `ImGui::Begin` in `Application.cpp`
- Mesh loading / normal computation: `COFFParser.cpp`
- Tile size tuning: `TILE_SIZE` in `TileData.h` (must also update shader workgroup sizes)
- Render pipeline orchestration: `Renderer.cpp`
- Event system: `EventBus.h` (subscribe/publish), `Events.h` (event structs)
- Entity structure: `Entity.h` (add new optional components here)
