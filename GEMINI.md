# SplatEngine Development Guide

This document provides architectural context, implementation details, and conventions for the **SplatEngine** project. Use this as a foundation for understanding the codebase and implementing changes.

---

## 1. Project Philosophy & Core Technique

**Triangle Splatting** is a compute-shader-driven differentiable rendering technique. Unlike traditional Gaussian Splatting, it maps more directly to GPU hardware but here it is implemented **entirely in compute shaders** for research flexibility.

- **No Graphics Pipeline:** No vertex/fragment shaders for geometry. Everything (projection, binning, rasterization) is compute-based.
- **Tiled Rendering:** The screen is divided into 16x16 tiles. Triangles are binned per-tile to optimize per-pixel work.
- **Atomic Depth:** Uses `imageAtomicMin` on a `R32UI` texture for depth testing in compute.

---

## 2. Key Architecture Components

| Component | Responsibility | Relevant Files |
|-----------|----------------|----------------|
| **Application** | Window management (GLFW), ImGui editor UI, and main loop orchestration. | `Application.h/cpp` |
| **Renderer** | Encapsulates the compute pipeline, viewport targets, and manages GPU resources. | `Renderer.h/cpp` |
| **Scene & Entity** | Manages scene objects (`Entity`), transforms, and loaded mesh assets. | `Scene.h/cpp`, `Entity.h`, `Transform.h` |
| **Camera** | FPS-style 6DOF navigation, view/proj matrices. | `Camera.h/cpp` |
| **Parser** | Loads `.off` (OFF) and `.coff` (Colored OFF) mesh files. | `COFFParser.h/cpp` |
| **GPUBuffer** | Manages Mesh SSBOs (Vertices and Faces). | `GPUBuffer.h/cpp` |
| **TileRasterizer** | Manages the tiling pipeline buffers (counts, offsets, bins). | `TileRasterizer.h/cpp`, `TileData.h` |
| **ComputeShader** | Utility for compiling and dispatching compute shaders. | `ComputeShader.h/cpp` |

---

## 3. The 4-Phase Rendering Pipeline

The pipeline is orchestrated in `Renderer.cpp`. Every change to geometry or shading must respect these phases:

1. **Phase 0: Clear** (`clear_depth.comp`, `clear_tiles.comp`)
   - Clears the depth buffer to `0xFFFFFFFF`.
   - Clears tile counters to 0.

2. **Phase 1: Project & Count** (`pass1_count.comp`)
   - Projects 3D vertices to 2D screen space.
   - Calculates AABB for each triangle.
   - For each tile overlapped by the AABB, increments that tile's counter in `TileCounterBuffer`.

3. **Phase 1.5: Prefix Sum** (`pass2_prefix_sum.comp`)
   - Performs an exclusive prefix sum on `TileCounterBuffer` to produce `TileOffsetBuffer`.
   - The last element of `TileOffsetBuffer` tells the CPU how many total triangle-tile pairs were generated.

4. **Phase 2: Binning** (`pass3_bin.comp`)
   - Re-iterates triangles and places their indices into a flat `TriangleListBuffer` at the offsets calculated in Phase 1.5.
   - Uses `TileWriteOffsetBuffer` (an atomic copy of counters) to track local binning progress.

5. **Phase 3: Rasterization** (`pass4_rasterize_tiled.comp`)
   - Each compute thread (pixel) identifies its tile.
   - Iterates through the binned triangles for that tile.
   - Performs edge-function point-in-triangle tests and barycentric depth interpolation.
   - Updates `outputImage` and `depthBuffer` using `imageAtomicMin`.

---

## 4. GPU Data Layout (Critical for Shader Changes)

All structs use **std430** layout. Padding is explicit in C++ to match GLSL's 16-byte alignment requirements for `vec3/vec4`.

### Vertex (32 bytes)
```cpp
struct Vertex {
    glm::vec3 position; // 12 bytes
    float _pad0;        // 4 bytes (align to 16)
    glm::vec3 padding;  // 12 bytes
    float _pad1;        // 4 bytes (align to 32)
};
```

### Face (32 bytes)
```cpp
struct Face {
    glm::uvec3 indices; // 12 bytes
    unsigned int _pad0; // 4 bytes (align to 16)
    glm::vec3 color;    // 12 bytes
    float padding;      // 4 bytes (align to 32)
};
```

### ProjectedTriangle (72 bytes)
Defined in `TileData.h`. Holds 2D projected data, NDC depths, and screen-space AABB.

---

## 5. Development Conventions

- **Shader Updates:** Shaders are copied from `shaders/` to the build directory post-build. If you modify a shader, you must re-build (or manually copy) for changes to take effect.
- **Memory Barriers:** Always use `glMemoryBarrier` between compute passes. Common bits: `GL_SHADER_STORAGE_BARRIER_BIT`, `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`.
- **Coordinate Systems:**
  - World space: Right-handed.
  - Screen space: (0,0) is bottom-left (standard OpenGL texture/image layout).
- **Adding a New Shader:**
  1. Add `.comp` file to `shaders/`.
  2. Instantiate `ComputeShader` in `Renderer.cpp`.
  3. Dispatch in the appropriate phase in `Renderer::render()`.

---

## 6. Common Tasks & Where to Edit

- **Change Camera Behavior:** Modify `Camera.cpp`.
- **Change Rasterization Logic (e.g., add blending):** Modify `pass4_rasterize_tiled.comp`.
- **Add New UI/Editor Features:** Modify `Application::update()` in `Application.cpp`.
- **Modify Mesh Loading:** Edit `COFFParser.cpp`.
- **Adjust Tiling Performance:** Change `TILE_SIZE` in `TileData.h` (and update shader thread-group sizes accordingly).
- **Manipulate the Scene:** Use methods in `Scene.h` and configure `Entity`s.

---

## 7. LLM Instructions for Maintainers

- **Safety:** Do not remove the `std430` padding in C++ structs; it will break GPU/CPU communication.
- **Validation:** When adding new SSBOs, ensure binding points are unique and consistent between `Renderer.cpp` and `.comp` files.
- **Context:** Always refer back to `README.md` for high-level research context and references.
