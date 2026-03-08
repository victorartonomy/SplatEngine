# SplatEngine

A real-time rendering engine built on **Triangle Splatting** — a differentiable rendering technique that represents 3D scenes as explicit triangle primitives with smooth, SDF-based window functions. Unlike 3D Gaussian Splatting (which relies on custom tile rasterizers and volumetric ellipsoids), Triangle Splatting maps directly to standard GPU triangle rasterization hardware, enabling native compatibility with existing graphics pipelines.

> **Status:** Research Prototype

![Screenshot Placeholder](docs/screenshot_placeholder.png)
*Screenshots coming soon.*

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Test Models](#test-models)
- [Usage & Controls](#usage--controls)
- [Architecture](#architecture)
- [Rendering Pipeline](#rendering-pipeline)
- [GPU Data Layout](#gpu-data-layout)
- [Shader Reference](#shader-reference)
- [Project Structure](#project-structure)
- [Key Constants](#key-constants)
- [Known Limitations](#known-limitations)
- [References](#references)

---

## Features

- **Compute-shader-only rasterization** — no traditional GL rasterization pipeline; all triangle projection, tiling, depth testing, and shading run as compute dispatches
- **Tile-based rendering** — screen divided into 16×16 pixel tiles; triangles binned per-tile via GPU prefix sum for efficient per-pixel iteration
- **Atomic depth buffer** — `imageAtomicMin` on R32UI for compute-shader depth testing
- **OFF/COFF mesh loading** — parser supports both formats with optional per-face RGB colors (auto-normalizes 0–255 to 0.0–1.0)
- **FPS camera** — full 6DOF movement, mouse look, scroll-wheel FOV adjustment, preset viewpoint hotkeys
- **Debug visualizations** — tile checkerboard overlay and per-tile triangle count heatmap

---

## Requirements

### Hardware

| Component | Minimum | Tested |
|-----------|---------|--------|
| GPU | OpenGL 4.3+ with `GL_ARB_gpu_shader_int64` | NVIDIA RTX 4060 |
| VRAM | Depends on model size | — |

### Software

| Dependency | Version | Notes |
|------------|---------|-------|
| **CMake** | 3.20+ | Build system |
| **C++ Compiler** | C++20 support | MSVC 17+, GCC 11+, Clang 14+ |
| **Ninja** | Any recent | Build generator |
| **Git** | Any recent | Required for FetchContent dependency downloads |

The following libraries are **fetched automatically** during CMake configuration (no manual installation needed):

| Library | Version | Purpose |
|---------|---------|---------|
| [GLFW](https://github.com/glfw/glfw) | 3.4 | Window creation, input handling |
| [GLM](https://github.com/g-truc/glm) | 1.0.1 | Mathematics (vectors, matrices) |
| [GLAD](https://glad.dav1d.de/) | Included in `src/glad.c` | OpenGL function loader |

---

## Building

### Windows (Visual Studio / MSVC)

```bash
# Clone the repository
git clone https://github.com/victorartonomy/SplatEngine.git
cd SplatEngine

# Configure with CMake (Ninja generator)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

The executable will be at `build/Engine.exe`. Shaders and models are copied to the build directory automatically via post-build commands.

### Linux (GCC / Clang)

```bash
# Install system dependencies (Ubuntu/Debian)
sudo apt install cmake ninja-build build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev

# Clone and build
git clone https://github.com/victorartonomy/SplatEngine.git
cd SplatEngine
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Visual Studio (Open Folder / CMake)

1. Open the repository folder in Visual Studio
2. Visual Studio auto-detects `CMakeLists.txt` and configures via `out/build/x64-Debug`
3. Select `Engine.exe` as the startup target and run

---

## Test Models

The engine loads meshes in **OFF** (Object File Format) or **COFF** (Colored OFF) format.

Place `.off` files in the `models/` directory. The default model path is `models/room.off` (hardcoded in `src/main.cpp`).

> **Note:** Model files (`.off`) are excluded from the repository via `.gitignore` due to their size. Download link will be provided here.

### OFF Format Reference

```
OFF
<num_vertices> <num_faces> <num_edges>
x y z                          # per vertex (float)
3 v0 v1 v2 [r g b [a]]        # per face (indices + optional color)
```

Colors > 1.0 are treated as 0–255 range and auto-normalized. Faces without colors default to `(0.8, 0.8, 0.8)`.

---

## Usage & Controls

```bash
# Run from the build directory
./Engine          # Linux
Engine.exe        # Windows
```

### Keyboard

| Key | Action |
|-----|--------|
| `W A S D` | Move forward / left / backward / right |
| `Space` | Move up |
| `Left Shift` | Move down |
| `Mouse` | Look around |
| `Scroll Wheel` | Adjust FOV (1°–90°) |
| `P` | Print camera position and mesh bounds to console |
| `0` | Toggle debug tile visualization |
| `1` | Preset: far overview of mesh |
| `2` | Preset: close front view |
| `3` | Preset: top-down view |
| `4`–`9` | Preset: various axis-aligned views |
| `Esc` | Exit |

---

## Architecture

The engine follows a **compute-shader-driven** architecture. There is no traditional vertex/fragment rasterization for scene geometry — all projection, binning, depth testing, and shading execute as compute shader dispatches. A single fullscreen quad with a fragment shader blits the final compute output texture to the screen.

### High-Level Diagram

```
┌─────────────────────────────────────────────────────────┐
│                     main.cpp                            │
│              (Render Loop Orchestration)                 │
├──────────┬──────────┬───────────┬───────────────────────┤
│ Camera   │ COFFParser│ GPUBuffer │   TileRasterizer      │
│ (View/   │ (OFF→Mesh)│ (SSBO     │   (Tile buffer mgmt,  │
│  Proj)   │          │  upload)  │    prefix sum readback)│
├──────────┴──────────┴───────────┴───────────────────────┤
│                 ComputeShader / Shader                   │
│          (Compile, dispatch, uniform setters)            │
├─────────────────────────────────────────────────────────┤
│           OpenGL 4.3 Core  (GLAD + GLFW)                │
└─────────────────────────────────────────────────────────┘
```

### Class Responsibilities

| Class | Files | Role |
|-------|-------|------|
| `Camera` | `Camera.h/cpp` | FPS camera: view/projection matrices, keyboard/mouse/scroll input, `lookAt` |
| `COFFParser` | `COFFParser.h/cpp` | Static parser: OFF/COFF → `Mesh`. Handles per-face RGB, non-triangle skipping |
| `Mesh` | `Mesh.h` | POD container: `vector<Vertex>` + `vector<Face>` with GPU-aligned structs |
| `GPUBuffer` | `GPUBuffer.h/cpp` | Uploads `Mesh` data to two SSBOs (vertex + face). Move-only semantics |
| `ComputeShader` | `ComputeShader.h/cpp` | Loads/compiles `.comp` files, dispatches with auto memory barrier, uniforms via `glProgramUniform*` |
| `Shader` | `Shader.h` | Loads/compiles vertex+fragment pair for display pass |
| `TileRasterizer` | `TileRasterizer.h/cpp` | Allocates/manages 6 SSBOs for the tile pipeline, CPU readback of total triangle-tile pairs |

---

## Rendering Pipeline

Each frame executes the following phases in order:

```
Phase 0 ─ Clear
│  clear_depth.comp   →  Depth buffer (R32UI) set to 0xFFFFFFFF
│  clear_tiles.comp   →  Tile counters (uint[]) set to 0
│  ── memory barrier ──
│
Phase 1 ─ Project & Count
│  pass1_count.comp   →  Project all triangles (VP matrix → NDC → screen)
│                        Compute screen-space AABB per triangle
│                        atomicAdd to tileCounters[] for each overlapping tile
│                        Culls: w ≤ 0, zero-area AABB, >1024 tile coverage
│  ── memory barrier ──
│
Phase 1.5 ─ Prefix Sum (3 dispatches)
│  pass2_prefix_sum.comp
│    pass=0  →  Per-block Blelloch exclusive scan (256 elements/block)
│    pass=1  →  Scan block sums (1 workgroup, numElements blocks)
│    pass=2  →  Propagate block sums + compute grand total
│  ── memory barrier (includes GL_BUFFER_UPDATE_BARRIER_BIT) ──
│  CPU readback: tileOffsets[totalTiles] → totalPairs
│
Phase 2 ─ Bin Triangles
│  pass3_bin.comp     →  Scatter triangle IDs into per-tile lists
│                        using prefix-sum offsets + atomic write counters
│  ── memory barrier ──
│
Phase 3 ─ Rasterize
│  pass4_rasterize_tiled.comp
│    Each pixel: find tile → iterate tile's triangle list
│    Edge-function point-in-triangle test
│    Barycentric depth interpolation
│    imageAtomicMin depth test (R32UI)
│    imageStore face.color on depth win
│  ── memory barrier ──
│
Phase 4 ─ Display
│  fullscreen.vert + fullscreen.frag
│    Draw fullscreen quad, sample output texture
│    glfwSwapBuffers
```

### Coordinate Flow

```
World Space (OFF vertex positions)
  → View Space         glm::lookAt(position, position + front, up)
  → Clip Space         glm::perspective(fov, aspect, 0.1, 10000)
  → NDC                clip.xyz / clip.w
  → Screen Pixels      x = (ndc.x * 0.5 + 0.5) * width
                        y = (ndc.y * 0.5 + 0.5) * height
```

---

## GPU Data Layout

All structs use **std430** layout and are explicitly padded to match between C++ and GLSL.

### Vertex — 32 bytes

| Offset | C++ Field | GLSL Field | Type | Bytes |
|--------|-----------|------------|------|-------|
| 0 | `position` | `position` | vec3 | 12 |
| 12 | `_pad0` | *(implicit)* | float | 4 |
| 16 | `padding` | `padding` | vec3 | 12 |
| 28 | `_pad1` | *(implicit)* | float | 4 |

### Face — 32 bytes

| Offset | C++ Field | GLSL Field | Type | Bytes |
|--------|-----------|------------|------|-------|
| 0 | `indices` | `indices` | uvec3 | 12 |
| 12 | `_pad0` | *(implicit)* | uint | 4 |
| 16 | `color` | `color` | vec3 | 12 |
| 28 | `padding` | `padding` | float | 4 |

### ProjectedTriangle — 72 bytes

| Offset | Field | Type | Bytes |
|--------|-------|------|-------|
| 0 | `v0` (screen pos) | vec2 | 8 |
| 8 | `v1` | vec2 | 8 |
| 16 | `v2` | vec2 | 8 |
| 24 | `depth0` (NDC z) | float | 4 |
| 28 | `depth1` | float | 4 |
| 32 | `depth2` | float | 4 |
| 36 | `_pad0` | float | 4 |
| 40 | `aabbMin` (pixels) | ivec2 | 8 |
| 48 | `aabbMax` (pixels) | ivec2 | 8 |
| 56 | `originalFaceIndex` | uint | 4 |
| 60 | `padding[3]` | uint×3 | 12 |

---

## Shader Reference

### SSBO Binding Map Per Pass

**Pass 1** (`pass1_count.comp`) — workgroup 256×1×1

| Binding | Buffer |
|---------|--------|
| 0 | VertexBuffer |
| 1 | FaceBuffer |
| 2 | ProjectedTriangleBuffer |
| 3 | TileCounterBuffer |

*Uniforms:* `mat4 viewProjection`, `ivec2 screenSize`, `ivec2 numTiles`

**Pass 2** (`pass2_prefix_sum.comp`) — workgroup 256×1×1

| Binding | Buffer |
|---------|--------|
| 0 | TileCounterBuffer |
| 1 | TileOffsetBuffer (totalTiles + 1 entries) |
| 2 | BlockSumBuffer |

*Uniforms:* `uint numElements`, `uint pass`

**Pass 3** (`pass3_bin.comp`) — workgroup 256×1×1

| Binding | Buffer |
|---------|--------|
| 0 | ProjectedTriangleBuffer |
| 1 | TileCounterBuffer |
| 2 | TileOffsetBuffer |
| 3 | TriangleListBuffer |
| 4 | TileWriteOffsetBuffer |

*Uniforms:* `ivec2 numTiles`, `uint maxTriangleListSize`

**Pass 4** (`pass4_rasterize_tiled.comp`) — workgroup 16×16×1

| Binding | Type | Resource |
|---------|------|----------|
| 0 | image2D | outputImage (RGBA32F) |
| 1 | uimage2D | depthBuffer (R32UI) |
| 2 | SSBO | VertexBuffer |
| 3 | SSBO | FaceBuffer |
| 4 | SSBO | ProjectedTriangleBuffer |
| 5 | SSBO | TileOffsetBuffer |
| 6 | SSBO | TriangleListBuffer |

*Uniforms:* `ivec2 screenSize`, `ivec2 numTiles`

---

## Project Structure

```
SplatEngine/
├── CMakeLists.txt              # Build configuration, FetchContent deps
├── README.md
├── .gitignore
│
├── include/
│   ├── Camera.h                # FPS camera class
│   ├── COFFParser.h            # OFF/COFF mesh loader
│   ├── ComputeShader.h         # Compute shader wrapper
│   ├── GPUBuffer.h             # SSBO mesh uploader
│   ├── Mesh.h                  # Vertex, Face, Mesh structs
│   ├── Shader.h                # Vert+Frag shader wrapper
│   ├── TileData.h              # ProjectedTriangle, TileInfo
│   └── TileRasterizer.h        # Tile pipeline buffer manager
│
├── src/
│   ├── main.cpp                # Entry point, render loop, pipeline orchestration
│   ├── Camera.cpp
│   ├── COFFParser.cpp
│   ├── ComputeShader.cpp
│   ├── GPUBuffer.cpp
│   ├── Shader.cpp
│   ├── TileRasterizer.cpp
│   └── glad.c                  # GLAD OpenGL loader (vendored)
│
├── shaders/
│   ├── clear_depth.comp        # Clears R32UI depth to 0xFFFFFFFF
│   ├── clear_tiles.comp        # Clears tile counters to 0
│   ├── pass1_count.comp        # Triangle projection + per-tile counting
│   ├── pass2_prefix_sum.comp   # 3-pass Blelloch exclusive prefix sum
│   ├── pass3_bin.comp          # Triangle-to-tile binning
│   ├── pass4_rasterize_tiled.comp  # Per-pixel tiled rasterization
│   ├── rasterize.comp          # Legacy brute-force rasterizer (unused)
│   ├── debug_tiles.comp        # Checkerboard tile overlay
│   ├── debug_heatmap.comp      # Per-tile triangle count heatmap
│   ├── fullscreen.vert         # Fullscreen quad vertex shader
│   └── fullscreen.frag         # Texture blit fragment shader
│
└── models/
    └── (place .off files here)
```

---

## Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| Window size | 1280 × 720 | `main.cpp` |
| OpenGL version | 4.3 Core | `main.cpp` |
| Tile size | 16 × 16 pixels | `TileData.h`, all shaders |
| Tiles at default res | 80 × 45 = 3600 | Computed |
| Prefix sum block | 256 elements | `pass2_prefix_sum.comp` |
| Tile coverage cap | 1024 tiles/triangle | `pass1_count.comp` |
| Triangle list pre-alloc | maxTriangles × 8 | `TileRasterizer.cpp` |
| Camera speed | 50 units/sec | `Camera.cpp` |
| Mouse sensitivity | 0.1 | `Camera.cpp` |
| FOV range | 1°–90° (default 45°) | `Camera.cpp` |
| Near/far planes | 0.1 / 10000 | `Camera.cpp` |
| Depth encoding | NDC z ∈ [-1,1] → uint32 | `pass4_rasterize_tiled.comp` |

---

## Known Limitations

- **Depth test race condition** — `imageAtomicMin` and `imageStore` are not jointly atomic; concurrent triangle writes to the same pixel may produce the wrong color on ties
- **`rasterize.comp` is legacy** — the brute-force per-pixel rasterizer (iterates all faces per pixel) is still present but unused in the active tiled path
- **No alpha blending** — the current pipeline renders fully opaque geometry only (Triangle Splatting+ Path B); the soft SDF window function path is not yet implemented
- **No spherical harmonics** — view-dependent color via SH coefficients is not yet implemented; faces use flat per-face RGB
- **Hardcoded model path** — `models/room.off` is hardcoded in `main.cpp`
- **Single-level prefix sum** — the Blelloch scan supports up to 256 blocks (65,536 tiles); screens larger than ~4096×4096 at tile size 16 would require an additional scan level

---

## References

1. Kerbl, B., Kopanas, G., Leimkühler, T., & Drettakis, G. (2023). *3D Gaussian Splatting for Real-Time Radiance Field Rendering.* ACM Transactions on Graphics (SIGGRAPH).

2. Rückert, D., Franke, L., & Stamminger, M. (2022). *ADOP: Approximate Differentiable One-Pixel Point Rendering.* ACM Transactions on Graphics.

3. Srinivasan, P. P., et al. (2021). *NeRV: Neural Reflectance and Visibility Fields for Relighting and View Synthesis.* CVPR.

4. Guedon, A., & Lepetit, V. (2024). *3D Gaussian Splatting as Markov Chain Monte Carlo.* arXiv:2404.09591.

5. Hahlbohm, M., Kappel, M., Kuth, J.-P., Gürtler, T., Frolov, V., & Eisemann, M. (2025). *Triangle Splatting for Real-Time Radiance Field Rendering.* Eurographics.

---

*This project is a personal research prototype and is not accepting external contributions at this time.*
