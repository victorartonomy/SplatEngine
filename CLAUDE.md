# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

`cmake` is not on the system PATH. Use MSBuild or the VS-bundled cmake directly:

```bash
# Configure (first time only)
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -B build -G "Visual Studio 18 2026" -A x64

# Build (Release)
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" build\Engine.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal

# Run
build\Release\Engine.exe
```

Dependencies (GLFW, GLM, Dear ImGui docking branch, Native File Dialog Extended, GLAD) are all auto-fetched via FetchContent — no manual installs needed.

Shaders and models are auto-copied to the build directory via post-build commands. After editing a shader, you must rebuild (or manually copy the file) for changes to take effect.

There are no tests or linting configured.

## Architecture

This is a **Triangle Splatting** renderer — a compute-shader-only rendering engine. There is NO traditional vertex/fragment graphics pipeline for geometry. All projection, binning, rasterization, depth testing, and transparency run as compute shader dispatches. The screen is divided into 16×16 pixel tiles.

### Rendering Pipeline (orchestrated in `Renderer.cpp`)

| Phase | Shader | Purpose |
|-------|--------|---------|
| 0: Clear | `clear_depth.comp`, `clear_tiles.comp` | Reset depth buffer (R32UI) to 0xFFFFFFFF and tile counters to 0 |
| 0.5: Shadow | `clear_shadow.comp`, `shadow_pass.comp` | Render scene depth from the primary light into a 2048×2048 R32UI shadow map |
| 1: Project & Count | `pass1_count.comp` | Project triangles to screen space, compute AABBs, atomically count triangles per tile |
| 1.5: Prefix Sum | `pass2_prefix_sum.comp` (3 dispatches) | Blelloch exclusive scan on tile counters; CPU readback of total triangle-tile pairs |
| 2: Bin | `pass3_bin.comp` | Scatter triangle IDs into per-tile lists using prefix-sum offsets |
| 3: Rasterize | `pass4_rasterize_tiled.comp` | Per-pixel edge tests, depth/normal/position interpolation, Blinn-Phong or PBR shading, WBOIT accumulation for transparent fragments |
| 4: OIT Composite | `pass5_oit_composite.comp` | Blend the weighted transparent layer over the opaque output image |
| 5: Display | ImGui viewport | Sample output texture and present |

### Transparency System (WBOIT)

`pass4` splits each fragment at the `mat.alpha` threshold:
- `alpha >= 0.99` → **opaque path**: `imageAtomicMin` depth test, winner writes to `outputImage`.
- `alpha < 0.99` → **transparent path**: WBOIT (McGuire & Bavoil 2013). Each pixel thread accumulates `color × alpha × weight` into thread-local registers and writes to five OIT SSBOs (bindings 9–13) at the end of `main()`. No atomics are needed because each pixel belongs to exactly one tile (one thread owns each pixel).

`pass5` composites: `transColor * (1 - reveal) + opaqueColor * reveal`.

Per-material alpha is exposed in the **Materials** ImGui panel (0 = transparent, 1 = opaque). The default material always has `alpha = 1.0` so existing meshes are unaffected.

### Lighting System

- **Light types:** Directional, Point, Spot (inner/outer cone falloff)
- **Shading models:** Blinn-Phong and PBR metallic-roughness (GGX/Smith/Schlick), switchable at runtime
- **Shadow mapping:** One directional light can cast shadows via the R32UI shadow map. `castsShadow` and `lightViewProjection` are baked into `GPULight`.
- **Light data** is uploaded as a `GPULight` SSBO at binding 7. `LightManager` rebuilds this buffer each frame. If no light entities exist a default directional light is used.

### Event System

Synchronous pub/sub via `EventBus.h/cpp`:
- `WindowResizeEvent` → `Renderer::resize()`
- `AssetLoadedEvent` → fired after GPU upload (sync and async paths)
- `ActionEvent` → published on input rising/falling edge

### Key Components

| Component | Files | Role |
|-----------|-------|------|
| Application | `Application.h/cpp` | Render loop, ImGui panels, scene/camera management |
| Renderer | `Renderer.h/cpp` | Orchestrates the full compute pipeline; owns GPU resources |
| LightManager | `LightManager.h/cpp` | Builds `GPULight` SSBO, manages shadow VP, shading model |
| MaterialManager | `MaterialManager.h/cpp` | Owns the `GPUMaterial` SSBO (binding 8); slot 0 is the default |
| TextureManager | `TextureManager.h/cpp` | Owns the `GL_TEXTURE_2D_ARRAY` sampled in pass4 as `textureAtlas` |
| TileRasterizer | `TileRasterizer.h/cpp` | Allocates/resizes the 6 tiling pipeline SSBOs; handles prefix-sum CPU readback |
| COFFParser | `COFFParser.h/cpp` | Loads OFF/COFF files, computes smooth per-vertex normals (area-weighted), generates planar UVs |
| ComputeShader | `ComputeShader.h/cpp` | Loads `.comp` files; uniform API: `setInt/UInt/Float/Vec3/IVec2/Mat4` |
| AssetManager | `AssetManager.h/cpp` | Mesh loading, caching, ref-counting, async support |
| Entity | `Entity.h` | Transform + optional mesh + optional `Light` (`std::optional<Light>`) |
| Camera | `Camera.h/cpp` | FPS 6DOF (quaternion), orbit mode, bookmarks |
| EventBus | `EventBus.h/cpp` | Type-safe synchronous pub/sub via `std::type_index`; `bus()` singleton |

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

### pass4_rasterize_tiled.comp + pass5_oit_composite.comp
| Binding | Type | Buffer/Image |
|---------|------|-------------|
| image unit 0 | `image2D` (RGBA32F) | outputImage — write-only in pass4, read-write in pass5 |
| image unit 1 | `uimage2D` (R32UI) | depthBuffer (imageAtomicMin) |
| image unit 2 | `uimage2D` (R32UI) | shadowMap (read-only in pass4) |
| 2 | SSBO | VertexBuffer |
| 3 | SSBO | FaceBuffer |
| 4 | SSBO | ProjectedTriangleBuffer |
| 5 | SSBO | TileOffsetBuffer |
| 6 | SSBO | TriangleListBuffer |
| 7 | SSBO | LightBuffer (`GPULight[]`) |
| 8 | SSBO | MaterialBuffer (`GPUMaterial[]`) |
| 9 | SSBO | OIT oitAccumR — `sum(r × alpha × weight)` |
| 10 | SSBO | OIT oitAccumG |
| 11 | SSBO | OIT oitAccumB |
| 12 | SSBO | OIT oitAccumA — `sum(alpha × weight)` |
| 13 | SSBO | OIT oitReveal — `product(1 − alpha_i)` |

Image units and SSBO binding points are **separate namespaces**. `GL_MAX_IMAGE_UNITS` caps image units (often 8); SSBO bindings are separate.

## GPU Struct Sizes (std430 — must not change without updating GLSL counterparts)

| Struct | File | Size | Notes |
|--------|------|------|-------|
| `Vertex` | `Mesh.h` | 48 bytes | vec3 position + pad, vec3 normal + pad, vec2 uv + 2 pads |
| `Face` | `Mesh.h` | 32 bytes | uvec3 indices + pad, vec3 color, uint materialID |
| `GPUMaterial` | `Mesh.h` | 32 bytes | albedo, metallic, roughness, emissive, textureID, **alpha** |
| `GPULight` | `Light.h` | **128 bytes** | 64 bytes of scalars + mat4 lightViewProjection; `static_assert` enforced |
| `ProjectedTriangle` | `TileData.h` | 72 bytes | screen verts, NDC depths, AABB, faceIndex, padding[3] |

## Critical Constraints

- **std430 padding is load-bearing.** Explicit padding fields in `Mesh.h`, `TileData.h`, and `Light.h` match GLSL 16-byte alignment. Reordering or removing them silently corrupts GPU data. `GPULight` is guarded by `static_assert(sizeof(GPULight) == 128)`.
- **SSBO binding points** must stay unique and consistent between C++ and `.comp` files. Bindings 0–13 are now in use. New SSBOs start at binding 14.
- **Normal transform:** `Renderer::submitScene()` uses `transpose(inverse(mat3(model)))`. Never use the model matrix directly for normals.
- **Memory barriers** (`glMemoryBarrier`) are required between every compute pass: `GL_SHADER_STORAGE_BARRIER_BIT` between SSBO-producing and SSBO-consuming passes; `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT` before image reads; `GL_TEXTURE_FETCH_BARRIER_BIT` before ImGui samples the output texture.
- **Coordinate systems:** World space is right-handed. Screen space (0,0) is bottom-left (standard OpenGL).
- **OpenGL 4.3 Core** minimum. The shadow map and OIT paths use no extensions beyond what pass4 already required (`GL_ARB_gpu_shader_int64`).
- **No atomics on OIT SSBOs.** Each pixel is handled by exactly one thread (one workgroup per tile, 16×16 threads per workgroup, non-overlapping tiles). Plain SSBO stores in pass4 are safe.

## Where to Edit

- **Rasterization / shading / transparency:** `pass4_rasterize_tiled.comp` (opaque + WBOIT accumulation)
- **OIT compositing formula:** `pass5_oit_composite.comp`
- **Material parameters (CPU side):** `Mesh.h` (`GPUMaterial`), `MaterialManager.cpp` (defaults)
- **Material UI:** search `ImGui::Begin("Materials")` in `Application.cpp`
- **Light types and GPU struct:** `Light.h` — must keep `GPULight` at 128 bytes and update GLSL struct to match
- **Light / shadow management:** `LightManager.cpp`
- **Camera behavior:** `Camera.cpp`
- **Key bindings / input actions:** `InputManager.cpp` (`initialize()`)
- **Mesh loading / normal computation:** `COFFParser.cpp`
- **Tile size tuning:** `TILE_SIZE` in `TileData.h` — must also update shader `local_size_x/y`
- **Render pipeline orchestration:** `Renderer.cpp`
- **Event system:** `EventBus.h` (subscribe/publish), `Events.h` (event structs)
- **Entity structure:** `Entity.h` (add new optional components here)

## Adding a New Shader

1. Add `.comp` file to `shaders/`
2. Add `std::unique_ptr<ComputeShader>` member to `Renderer.h`
3. Load it in `Renderer::initialize()` and add to the validity check
4. Bind SSBOs/images and dispatch in `Renderer::render()` at the correct phase
5. Release in `Renderer::shutdown()`
6. Shaders are auto-copied to the build output directory on rebuild

## SplatVault — Knowledge Base

The Obsidian vault at `SplatVault/` is a living second brain for this project. **After every code change, update the relevant vault notes.**

### Vault Path
`D:\Create\Applications\SplatEngine\SplatVault\`

### What to Update After Each Change

| Change Made | Vault Notes to Update |
|-------------|----------------------|
| Add/rename/remove a field in `Vertex`, `Face`, `GPUMaterial`, `GPULight`, or `ProjectedTriangle` | `Mesh.md`, `Light.md`, `TileData.md`, `GPU Memory Layout.md`, every shader note that declares the struct |
| Add a new SSBO or change an existing binding point | `SSBO Binding Map.md`, every shader note that uses that binding |
| Add a new compute shader | Create `04 - Shaders/<shader_name>.md`; update `Renderer.md`, `Rendering Pipeline.md`, `Home.md` |
| Add a new C++ class or header | Create `03 - Source Files/<ClassName>.md`; update `Architecture Overview.md`, `Home.md` |
| Change the rendering pipeline order or add/remove a phase | `Rendering Pipeline.md`, the Mermaid flowchart in `Home.md` |
| Change `TILE_SIZE` in `TileData.h` | `TileData.md`, `Tiled Rasterization.md`, all shader notes that mention `local_size_x/y` |
| Add a new light type or shading model | `Light.md`, `LightManager.md`, `PBR Shading.md`, `pass4_rasterize_tiled.md` |
| Change the OIT/transparency algorithm | `Transparency (WBOIT).md`, `pass4_rasterize_tiled.md`, `pass5_oit_composite.md` |
| Add a new event type to `Events.h` | `Events.md` |
| Change shadow map resolution, algorithm, or bias | `Shadow Mapping.md`, `shadow_pass.md`, `clear_shadow.md`, `Light.md` |
| Add or remove a manager (LightManager, MaterialManager, etc.) | The manager's note + `Renderer.md`, `Architecture Overview.md` |

### Vault Structure Reminder

```
SplatVault/
├── Home.md                   ← Master index + pipeline Mermaid
├── 00 - Meta/                ← Conventions, update protocol
├── 01 - Architecture/        ← Pipeline, SSBO map, GPU layout, system overview
├── 02 - Concepts/            ← WBOIT, shadow, PBR, tiling, Blelloch
├── 03 - Source Files/        ← One note per .h/.cpp pair
└── 04 - Shaders/             ← One note per .comp file
```

### WikiLink Naming

- C++ class → exact class name: `[[Renderer]]`, `[[LightManager]]`
- Shader → filename without `.comp`: `[[pass4_rasterize_tiled]]`
- Concept → full title: `[[Transparency (WBOIT)]]`, `[[Tiled Rasterization]]`
