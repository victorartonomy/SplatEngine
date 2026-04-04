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

### Rendering Pipeline (orchestrated in `Application.cpp`)

| Phase | Shader | Purpose |
|-------|--------|---------|
| 0: Clear | `clear_depth.comp`, `clear_tiles.comp` | Reset depth buffer (R32UI) to 0xFFFFFFFF and tile counters to 0 |
| 1: Project & Count | `pass1_count.comp` | Project triangles to screen space, compute AABBs, atomically count triangles per tile |
| 1.5: Prefix Sum | `pass2_prefix_sum.comp` (3 dispatches) | Blelloch exclusive scan on tile counters; CPU readback of total triangle-tile pairs |
| 2: Bin | `pass3_bin.comp` | Scatter triangle IDs into per-tile lists using prefix-sum offsets |
| 3: Rasterize | `pass4_rasterize_tiled.comp` | Per-pixel edge-function tests, barycentric depth interpolation, imageAtomicMin depth, color write |
| 4: Display | ImGui viewport | Sample output texture and present |

### Key Components

- **Application** (`Application.h/cpp`) -- Render loop, ImGui editor UI, asset manager, scene management, camera controls
- **InputManager** (`InputManager.h/cpp`) -- Named action/axis polling, rebindable GLFW key mappings; `Action` and `Axis` enums defined here
- **Camera** (`Camera.h/cpp`, `CameraState.h`) -- FPS 6DOF camera with quaternion orientation, orbit mode, bookmarks
- **COFFParser** (`COFFParser.h/cpp`) -- Loads OFF/COFF mesh files with optional per-face RGB colors
- **GPUBuffer** (`GPUBuffer.h/cpp`) -- Manages Vertex (binding 0) and Face (binding 1) SSBOs
- **TileRasterizer** (`TileRasterizer.h/cpp`, `TileData.h`) -- Allocates and manages 6 SSBOs for the tiling pipeline, handles prefix sum CPU readback
- **ComputeShader** (`ComputeShader.h/cpp`) -- Loads/compiles .comp files, dispatches with memory barriers

## Critical Constraints

- **std430 padding is load-bearing.** C++ structs in `Mesh.h` and `TileData.h` have explicit padding fields to match GLSL 16-byte alignment. Removing or reordering padding breaks GPU/CPU data communication.
- **SSBO binding points** must stay unique and consistent between C++ code and .comp shader files. When adding new SSBOs, check existing bindings in both places.
- **Memory barriers** (`glMemoryBarrier`) are required between every compute pass. Use `GL_SHADER_STORAGE_BARRIER_BIT` and `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`.
- **Coordinate systems:** World space is right-handed. Screen space (0,0) is bottom-left (standard OpenGL).
- **OpenGL 4.3 Core** is the minimum required version.

## Adding a New Shader

1. Add `.comp` file to `shaders/`
2. Instantiate `ComputeShader` in `Application.cpp`
3. Dispatch in the appropriate phase of the render loop
4. Shaders auto-copy to build dir on rebuild

## Where to Edit

- Camera behavior: `Camera.cpp`
- Key bindings / input actions: `InputManager.cpp` (`initialize()`)
- Rasterization logic (blending, shading): `pass4_rasterize_tiled.comp`
- UI/editor features: search for `ImGui::Begin` in `Application.cpp`
- Mesh loading: `COFFParser.cpp`
- Tile size tuning: `TILE_SIZE` in `TileData.h` (must also update shader workgroup sizes)
