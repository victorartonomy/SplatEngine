#include "Application.h"
#include "EventBus.h"
#include "Events.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.h>

#include <iostream>
#include <algorithm>
#include <cstdint>
#include <filesystem>

// ============================================
// CONSTRUCTION / DESTRUCTION
// ============================================

Application::Application() = default;
Application::~Application() = default;

// ============================================
// INITIALIZE
// ============================================

// Boot the engine: create the GLFW window, load OpenGL via GLAD, set up Dear ImGui,
// initialize Native File Dialog, compile compute shaders, and load the default scene.
//
// Steps:
//   1. GLFW + window creation (OpenGL 4.3 Core, VSync on)
//   2. GLAD function pointer loading
//   3. ImGui context with docking enabled
//   4. NFD (file dialogs — failure here is non-fatal, dialogs are just disabled)
//   5. Renderer (compiles all 6 compute shaders)
//   6. Default mesh load (models/room.off — skipped if not present)
//   7. Event subscriptions (window resize, asset loaded, quit action)
bool Application::initialize() {
    // ------------------------------------------
    // 1. GLFW
    // ------------------------------------------
    if (!glfwInit()) {
        std::cerr << "[ERROR] Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Request OpenGL 4.3 Core — minimum required for compute shaders (GL_ARB_compute_shader)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight,
                                "Triangle Splat Engine", nullptr, nullptr);
    if (!m_window) {
        std::cerr << "[ERROR] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // Enable VSync (swap buffers at monitor refresh rate)

    m_inputManager.initialize(m_window);

    // ------------------------------------------
    // 2. GLAD
    // ------------------------------------------
    // Load all OpenGL function pointers via GLFW's getProcAddress callback.
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[ERROR] Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return false;
    }
    std::cout << "[INFO] OpenGL " << glGetString(GL_VERSION)
              << " | " << glGetString(GL_RENDERER) << std::endl;

    // ------------------------------------------
    // 3. DEAR IMGUI
    // ------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // ImGuiConfigFlags_DockingEnable allows panels to be dragged and docked together
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 430"); // Must match the GLSL version in compute shaders

    // ------------------------------------------
    // 4. NATIVE FILE DIALOG
    // ------------------------------------------
    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "[WARNING] NFD init failed — file dialogs disabled" << std::endl;
    }

    // ------------------------------------------
    // 5. RENDERER (shaders, GPU resources, tile rasterizer)
    // ------------------------------------------
    if (!m_renderer.initialize(256, 256)) {
        glfwTerminate();
        return false;
    }

    // ------------------------------------------
    // 8. TRY LOADING DEFAULT MESH
    // ------------------------------------------
    // Attempts to load the bundled room.off model. If not found, the scene starts empty
    // and the user can import a mesh through the Asset Manager panel.
    AssetHandle roomHandle = m_assetManager.load("models/room.off");
    if (roomHandle.isValid()) {
        Entity& e   = m_scene.createEntity("room");
        e.meshAsset = roomHandle;
        m_scene.recalculateSceneBounds(m_assetManager);
        // Auto-frame the camera to show the entire scene on startup
        m_camera.setPosition(m_scene.getSceneCenter() +
            glm::vec3(0.0f, m_scene.getSceneSize().y * 0.5f, m_scene.getSceneSize().z * 2.0f));
        m_camera.lookAt(m_scene.getSceneCenter());
        m_renderer.submitScene(m_scene, m_assetManager);
    } else {
        std::cout << "[INFO] No default mesh found — use Asset Manager to import" << std::endl;
    }

    // ------------------------------------------
    // 9. EVENT SUBSCRIPTIONS
    // ------------------------------------------
    // WindowResizeEvent: fired when the viewport panel changes size (detected in update()).
    // Forwards the new dimensions to Renderer::resize() to recreate textures and tile buffers.
    bus().subscribe<WindowResizeEvent>([this](const WindowResizeEvent& e) {
        m_renderer.resize(e.width, e.height);
    });

    // AssetLoadedEvent: fired by AssetManager after a successful load (sync or async).
    // Currently only logs to the console; could be used for scene graph refresh or progress bars.
    bus().subscribe<AssetLoadedEvent>([](const AssetLoadedEvent& e) {
        std::cout << "[EVENT] Asset loaded: " << e.path
                  << " (handle " << e.handle.id << ")" << std::endl;
    });

    // ActionEvent: fired by InputManager when a key's pressed/released state changes.
    // The Quit action (Escape key) closes the window on the rising edge (pressed == true).
    bus().subscribe<ActionEvent>([this](const ActionEvent& e) {
        if (e.action == Action::Quit && e.pressed)
            glfwSetWindowShouldClose(m_window, true);
    });

    // ------------------------------------------
    // 10. EDITOR STATE
    // ------------------------------------------
    m_lastFPSTime = glfwGetTime();

    std::cout << "[INFO] Editor ready — hold Right-Click in Viewport for camera control" << std::endl;
    return true;
}

// ============================================
// RUN
// ============================================

// Main loop: calls update() once per frame until the window is closed.
void Application::run() {
    while (!glfwWindowShouldClose(m_window)) {
        update();
    }
}

// ============================================
// UPDATE (one frame)
// ============================================

// Execute one frame: poll input, build ImGui panels, run compute pipeline, present.
//
// Frame structure:
//   1. Delta time and FPS counter update
//   2. GLFW event polling (keyboard, mouse, window events)
//   3. ImGui frame begin
//   4. InputManager update (key state + event publication)
//   5. ImGui dock layout (built once on first frame via m_firstLoop)
//   6. All ImGui windows (Viewport, Camera Settings, Scene Hierarchy, Asset Manager, Lighting)
//   7. Asset manager update (async GPU upload drain + deferred destruction)
//   8. Renderer compute pipeline dispatch
//   9. ImGui render to default framebuffer + buffer swap
void Application::update() {
    // --- Timing ---
    float currentFrame = static_cast<float>(glfwGetTime());
    m_deltaTime = currentFrame - m_lastFrame;
    m_lastFrame = currentFrame;

    // FPS counter: count frames in each 1-second window
    m_frameCount++;
    if (currentFrame - m_lastFPSTime >= 1.0) {
        m_currentFPS = static_cast<float>(m_frameCount);
        m_frameCount = 0;
        m_lastFPSTime = currentFrame;
    }

    glfwPollEvents(); // Deliver GLFW callbacks (keyboard, window resize, etc.)

    // --- ImGui frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    // m_viewportWasHovered is one frame stale, but that's acceptable — avoids reading
    // hovered state mid-frame before the viewport window is processed this frame
    m_inputManager.update(io, m_viewportWasHovered);

    // --- Build initial dock layout once ---
    // DockBuilderSplitNode divides the main dockspace into a 75/25 left/right split,
    // then splits the right side 50/50 top/bottom. Each panel is assigned to a region.
    // This runs only on m_firstLoop to establish the default layout; users can rearrange panels.
    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_firstLoop) {
        m_firstLoop = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImVec2 vpSize;
        vpSize.x = static_cast<float>(m_windowWidth);
        vpSize.y = static_cast<float>(m_windowHeight);
        ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

        // Split into left (75%) and right (25%) columns, then right into top/bottom halves
        ImGuiID dockLeft = 0, dockRight = 0, dockRightTop = 0, dockRightBottom = 0;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.75f, &dockLeft, &dockRight);
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.5f, &dockRightTop, &dockRightBottom);

        ImGui::DockBuilderDockWindow("###Viewport",       dockLeft);        // Viewport takes the left 75%
        ImGui::DockBuilderDockWindow("Scene Hierarchy",   dockRightTop);    // Upper-right: scene tree
        ImGui::DockBuilderDockWindow("Asset Manager",     dockRightTop);    // Tabbed with Scene Hierarchy
        ImGui::DockBuilderDockWindow("Camera Settings",   dockRightBottom); // Lower-right: camera/lighting
        ImGui::DockBuilderDockWindow("Lighting",          dockRightBottom); // Tabbed with Camera Settings
        ImGui::DockBuilderDockWindow("Materials",         dockRightBottom); // Tabbed with Camera Settings
        ImGui::DockBuilderDockWindow("Textures",          dockRightBottom); // Tabbed with Camera Settings
        ImGui::DockBuilderDockWindow("Mouse",             dockRightBottom); // Tabbed with Camera Settings
        ImGui::DockBuilderFinish(dockspaceId);
    }

    // =============================================
    // VIEWPORT WINDOW
    // =============================================
    // The viewport title embeds the FPS counter. The "###Viewport" suffix makes the
    // dock ID stable even when the title text changes each frame.
    char vpTitle[64];
    snprintf(vpTitle, sizeof(vpTitle), "Viewport  (%.0f FPS)###Viewport", m_currentFPS);
    ImGui::Begin(vpTitle);
    {
        // --- Resize detection ---
        // Check if the available content area has changed. Publish WindowResizeEvent to
        // trigger Renderer::resize() via the event bus subscription set up in initialize().
        ImVec2 avail = ImGui::GetContentRegionAvail();
        int newW = std::max(16, static_cast<int>(avail.x)); // Clamp to avoid zero-size textures
        int newH = std::max(16, static_cast<int>(avail.y));

        if (newW != m_renderer.getViewportWidth() || newH != m_renderer.getViewportHeight())
            bus().publish(WindowResizeEvent{newW, newH});

        // --- Camera FPS input (RMB held) ---
        // Only process camera movement when RMB is held and the cursor is inside the viewport.
        // This prevents camera movement while typing in ImGui text fields.
        if (m_inputManager.isRMBDown() && m_inputManager.isViewportHovered()) {
            if (m_inputManager.isActionDown(Action::CameraFwd))
                m_camera.processKeyboard(CameraMovement::FORWARD, m_deltaTime);
            if (m_inputManager.isActionDown(Action::CameraBack))
                m_camera.processKeyboard(CameraMovement::BACKWARD, m_deltaTime);
            if (m_inputManager.isActionDown(Action::CameraLeft))
                m_camera.processKeyboard(CameraMovement::LEFT, m_deltaTime);
            if (m_inputManager.isActionDown(Action::CameraRight))
                m_camera.processKeyboard(CameraMovement::RIGHT, m_deltaTime);
            if (m_inputManager.isActionDown(Action::CameraUp))
                m_camera.processKeyboard(CameraMovement::UP, m_deltaTime);
            if (m_inputManager.isActionDown(Action::CameraDown))
                m_camera.processKeyboard(CameraMovement::DOWN, m_deltaTime);

            float dx = m_inputManager.getAxis(Axis::MouseX);
            float dy = m_inputManager.getAxis(Axis::MouseY);
            if (dx != 0.0f || dy != 0.0f)
                m_camera.processMouseMovement(dx, -dy); // Negate Y: screen Y down, world Y up
        }

        // --- Camera look (Alt+LMB) — alternative to RMB for look-around ---
        if (m_inputManager.isLMBDown() && m_inputManager.isAltDown()
                && m_inputManager.isViewportHovered()) {
            float dx = m_inputManager.getAxis(Axis::MouseX);
            float dy = m_inputManager.getAxis(Axis::MouseY);
            if (dx != 0.0f || dy != 0.0f)
                m_camera.processMouseMovement(dx, -dy);
        }

        // --- Scroll wheel: adjust movement speed ---
        float scroll = m_inputManager.getAxis(Axis::MouseScrollY);
        if (m_inputManager.isViewportHovered() && scroll != 0.0f)
            m_camera.processMouseScroll(scroll);

        // --- MMB drag: orbit around scene center ---
        // Pivot defaults to scene AABB center if there are visible meshes; otherwise world origin.
        if (m_inputManager.isMMBDown() && m_inputManager.isViewportHovered()) {
            float dx = m_inputManager.getAxis(Axis::MouseX);
            float dy = m_inputManager.getAxis(Axis::MouseY);
            if (dx != 0.0f || dy != 0.0f) {
                glm::vec3 pivot = m_scene.hasVisibleMeshes()
                                      ? m_scene.getSceneCenter()
                                      : glm::vec3(0.0f);
                m_camera.processOrbit(dx, -dy, pivot);
            }
        }

        // --- Display compute output ---
        // UV coordinates (0,1) → (1,0) flip the V axis because OpenGL textures have origin
        // at bottom-left while ImGui's image origin is top-left.
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(m_renderer.getOutputTexture())),
                     ImVec2(static_cast<float>(m_renderer.getViewportWidth()),
                            static_cast<float>(m_renderer.getViewportHeight())),
                     ImVec2(0, 1), ImVec2(1, 0));

        m_viewportWasHovered = ImGui::IsWindowHovered();
    }
    ImGui::End();

    // =============================================
    // CAMERA SETTINGS WINDOW
    // =============================================
    ImGui::Begin("Camera Settings");
    {
        // Position, yaw, pitch, FOV, speed, sensitivity — all editable
        glm::vec3 pos = m_camera.getPosition();
        if (ImGui::DragFloat3("Position", &pos.x, 1.0f))
            m_camera.setPosition(pos);

        float yaw = m_camera.getYaw();
        if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f))
            m_camera.setYaw(yaw);

        float pitch = m_camera.getPitch();
        if (ImGui::SliderFloat("Pitch", &pitch, -180.0f, 180.0f))
            m_camera.setPitch(pitch);

        float fov = m_camera.getFOV();
        if (ImGui::SliderFloat("FOV", &fov, 1.0f, 90.0f))
            m_camera.setFOV(fov);

        float speed = m_camera.getMovementSpeed();
        if (ImGui::DragFloat("Speed", &speed, 1.0f, 1.0f, 1000.0f))
            m_camera.setMovementSpeed(speed);

        float sens = m_camera.getMouseSensitivity();
        if (ImGui::DragFloat("Sensitivity", &sens, 0.01f, 0.01f, 1.0f))
            m_camera.setMouseSensitivity(sens);

        // --- Presets ---
        ImGui::Separator();
        ImGui::Text("Presets");

        // Top-Down View: position camera directly above scene AABB, look down
        if (ImGui::Button("Top-Down View")) {
            if (m_scene.hasVisibleMeshes()) {
                m_camera.setPosition(glm::vec3(m_scene.getSceneCenter().x,
                                               m_scene.getSceneMax().y + m_scene.getSceneSize().y,
                                               m_scene.getSceneCenter().z));
                m_camera.lookAt(m_scene.getSceneCenter());
            } else {
                m_camera.setPosition(glm::vec3(0.0f, 500.0f, 0.0f));
                m_camera.setPitch(-89.0f);
            }
        }
        ImGui::SameLine();
        // Reset: return all camera properties to their defaults
        if (ImGui::Button("Reset to Origin")) {
            m_camera.setPosition(glm::vec3(0.0f, 0.0f, 100.0f));
            m_camera.setYaw(-90.0f);
            m_camera.setPitch(0.0f);
            m_camera.setFOV(45.0f);
            m_camera.setMovementSpeed(50.0f);
            m_camera.setMouseSensitivity(0.1f);
        }

        // --- Bookmarks ---
        // Save the full camera state (position, orientation, FOV, speed, sensitivity) and
        // restore it later. Useful for quickly jumping between scene viewpoints.
        ImGui::Separator();
        ImGui::Text("Bookmarks");

        if (ImGui::Button("Save Current State")) {
            CameraState s;
            s.name             = "Bookmark " + std::to_string(m_bookmarks.size() + 1);
            s.position         = m_camera.getPosition();
            s.orientation      = m_camera.getOrientation();
            s.yaw              = m_camera.getYaw();
            s.pitch            = m_camera.getPitch();
            s.fov              = m_camera.getFOV();
            s.movementSpeed    = m_camera.getMovementSpeed();
            s.mouseSensitivity = m_camera.getMouseSensitivity();
            m_bookmarks.push_back(s);
        }

        // List all bookmarks; each has Load and X (delete) buttons
        for (int i = 0; i < static_cast<int>(m_bookmarks.size()); i++) {
            ImGui::PushID(i);
            if (ImGui::Button("Load")) {
                const auto& b = m_bookmarks[i];
                m_camera.setPosition(b.position);
                m_camera.setOrientation(b.orientation); // Restores quaternion directly (no Euler drift)
                m_camera.setFOV(b.fov);
                m_camera.setMovementSpeed(b.movementSpeed);
                m_camera.setMouseSensitivity(b.mouseSensitivity);
            }
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                m_bookmarks.erase(m_bookmarks.begin() + i);
                ImGui::PopID();
                break; // Exit loop — iterator is now invalid after erase
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(m_bookmarks[i].name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::End();

    // =============================================
    // SCENE HIERARCHY WINDOW
    // =============================================
    ImGui::Begin("Scene Hierarchy");
    {
        auto& entities = m_scene.getEntities();

        // Clamp the selection index in case the previously selected entity was deleted
        if (m_selectedEntity >= static_cast<int>(entities.size()))
            m_selectedEntity = -1;

        // --- Entity list ---
        // Clickable list; hidden entities are labeled "[hidden]" to distinguish them
        ImGui::Text("Entities (%zu)", entities.size());
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(entities.size()); i++) {
            ImGui::PushID(i);
            bool selected = (m_selectedEntity == i);
            char label[128];
            snprintf(label, sizeof(label), "%s%s",
                     entities[i].name.c_str(),
                     entities[i].visible ? "" : " [hidden]");
            if (ImGui::Selectable(label, selected))
                m_selectedEntity = i;
            ImGui::PopID();
        }

        // --- Selected entity inspector ---
        if (m_selectedEntity >= 0 && m_selectedEntity < static_cast<int>(entities.size())) {
            ImGui::Separator();
            Entity& ent = entities[m_selectedEntity];

            // Editable name field
            char nameBuf[256];
            strncpy_s(nameBuf, sizeof(nameBuf), ent.name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                ent.name = nameBuf;

            // Transform: any change triggers a scene re-merge and bounds recalculation
            bool changed = false;
            changed |= ImGui::DragFloat3("Position", &ent.transform.position.x, 1.0f);
            changed |= ImGui::DragFloat3("Rotation", &ent.transform.rotation.x, 1.0f);
            changed |= ImGui::DragFloat3("Scale",    &ent.transform.scale.x, 0.01f, 0.001f, 100.0f);
            changed |= ImGui::Checkbox("Visible", &ent.visible);

            if (changed) {
                // Re-submit the merged mesh to apply the new transform and update tile buffers
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }

            // Mesh statistics — only shown if a valid mesh asset is attached
            const MeshAsset* asset = m_assetManager.get(ent.meshAsset);
            if (asset) {
                ImGui::Text("Mesh: %s", std::filesystem::path(asset->filePath).filename().string().c_str());
                ImGui::Text("%zu verts, %zu faces",
                            asset->mesh.getVertexCount(), asset->mesh.getFaceCount());
            }

            ImGui::Separator();

            // Duplicate: creates a shallow clone offset by 50 units on X,
            // increments the mesh asset refcount so cleanup doesn't destroy it prematurely
            if (ImGui::Button("Duplicate")) {
                Entity clone      = ent;
                clone.name        = ent.name + " (copy)";
                clone.transform.position += glm::vec3(50.0f, 0.0f, 0.0f);
                Entity& newEnt    = m_scene.createEntity(clone.name);
                newEnt.meshAsset  = clone.meshAsset;
                newEnt.transform  = clone.transform;
                newEnt.visible    = clone.visible;
                if (newEnt.meshAsset.isValid())
                    m_assetManager.addRef(newEnt.meshAsset); // Prevent premature GPU buffer free
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }
            ImGui::SameLine();

            // Delete: releases the mesh asset refcount and removes the entity from the scene
            if (ImGui::Button("Delete")) {
                AssetHandle handleToRelease = ent.meshAsset; // Copy before entity is removed
                uint32_t idToRemove = ent.id;
                m_scene.removeEntity(idToRemove);
                m_selectedEntity = -1;
                if (handleToRelease.isValid())
                    m_assetManager.release(handleToRelease); // Decrement refcount; may defer GPU free
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }

            // --- Light component ---
            // Only visible lights (entities with a light component) affect shading.
            // The Add/Remove buttons simply set/reset the std::optional<Light> field.
            ImGui::Separator();
            if (ent.light.has_value()) {
                ImGui::Text("Light Component");
                Light& light = *ent.light;

                const char* typeNames[] = { "Directional", "Point", "Spot" };
                int typeIdx = static_cast<int>(light.type);
                if (ImGui::Combo("Light Type", &typeIdx, typeNames, 3))
                    light.type = static_cast<LightType>(typeIdx);

                ImGui::ColorEdit3("Light Color", &light.color.x);
                ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);

                // Range is only meaningful for attenuated lights (Point and Spot)
                if (light.type == LightType::Point || light.type == LightType::Spot)
                    ImGui::DragFloat("Range", &light.range, 1.0f, 1.0f, 10000.0f);

                // Cone angles are only meaningful for Spot lights
                // Enforce innerCone < outerCone to prevent the spotlight from inverting
                if (light.type == LightType::Spot) {
                    ImGui::DragFloat("Inner Cone", &light.innerCone, 0.5f, 1.0f, 89.0f);
                    ImGui::DragFloat("Outer Cone", &light.outerCone, 0.5f, 1.0f, 90.0f);
                    if (light.outerCone < light.innerCone)
                        light.outerCone = light.innerCone + 1.0f; // Maintain inner < outer invariant
                }

                if (ImGui::Button("Remove Light"))
                    ent.light.reset(); // Removes the Light component from this entity
            } else {
                if (ImGui::Button("Add Light"))
                    ent.light = Light{}; // Default-construct a Directional white light
            }
        }
    }
    ImGui::End();

    // =============================================
    // ASSET MANAGER WINDOW
    // =============================================
    ImGui::Begin("Asset Manager");
    {
        auto assetInfos = m_assetManager.getAssetInfos();

        // --- Loaded mesh assets list ---
        // Shows filename, vertex/face counts, reference count, and loading state for each asset.
        if (!assetInfos.empty()) {
            ImGui::Text("Mesh Assets (%zu)", assetInfos.size());
            ImGui::Separator();
            for (const auto& info : assetInfos) {
                // Count how many scene entities reference this asset (for editor info only)
                int refCount = 0;
                for (const auto& e : m_scene.getEntities())
                    if (e.meshAsset == info.handle) refCount++;

                ImGui::Text("%s", std::filesystem::path(info.filePath).filename().string().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu v, %zu f, %d ref%s)",
                                    info.vertexCount,
                                    info.faceCount,
                                    refCount,
                                    info.ready ? "" : " [loading]"); // "[loading]" for async loads
            }
            ImGui::Separator();
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "No meshes loaded");
            ImGui::Separator();
        }

        // --- Import ---
        // Opens a native OS file dialog (NFD) filtered to .off files.
        // On success: loads synchronously, creates a new scene entity, and auto-frames the camera.
        if (ImGui::Button("Import .off File")) {
            nfdu8char_t* outPath = nullptr;
            nfdu8filteritem_t filters[1] = { { "OFF Files", "off" } };
            nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);

            if (result == NFD_OKAY) {
                std::string newPath(outPath);
                NFD_FreePathU8(outPath);

                AssetHandle handle = m_assetManager.load(newPath);
                if (handle.isValid()) {
                    // Create a scene entity named after the file stem (e.g., "bunny" from "bunny.off")
                    std::string entityName =
                        std::filesystem::path(newPath).stem().string();
                    Entity& e    = m_scene.createEntity(entityName);
                    e.meshAsset  = handle;
                    m_scene.recalculateSceneBounds(m_assetManager);
                    // Auto-frame camera to show the newly imported mesh
                    m_camera.setPosition(m_scene.getSceneCenter() +
                        glm::vec3(0.0f,
                                  m_scene.getSceneSize().y * 0.5f,
                                  m_scene.getSceneSize().z * 2.0f));
                    m_camera.lookAt(m_scene.getSceneCenter());
                    m_renderer.submitScene(m_scene, m_assetManager);
                }
            } else if (result == NFD_ERROR) {
                std::cerr << "[ERROR] NFD: " << NFD_GetError() << std::endl;
            }
        }

        ImGui::SameLine();
        // Debug Tiles mode: replaces the rasterize pass with a tile heat map visualization
        bool dbg = m_renderer.getDebugMode();
        if (ImGui::Checkbox("Debug Tiles", &dbg))
            m_renderer.setDebugMode(dbg);
    }
    ImGui::End();

    // =============================================
    // LIGHTING WINDOW
    // =============================================
    ImGui::Begin("Lighting");
    {
        // Shading model selection: Blinn-Phong (fast) or PBR (physically accurate).
        // Forwarded to the pass4 shader as the "shadingModel" uniform.
        const char* modelNames[] = { "Blinn-Phong", "PBR (Metallic-Roughness)" };
        int modelIdx = static_cast<int>(m_renderer.getLightManager().getShadingModel());
        if (ImGui::Combo("Shading Model", &modelIdx, modelNames, 2))
            m_renderer.getLightManager().setShadingModel(static_cast<ShadingModel>(modelIdx));

        ImGui::Separator();

        // Quick-add buttons: create a light entity with sensible defaults.
        // Point and Spot lights are positioned above the scene AABB center if a mesh is loaded.
        if (ImGui::Button("Add Directional Light")) {
            Entity& e = m_scene.createEntity("Directional Light");
            e.light = Light{LightType::Directional};
            e.transform.rotation = glm::vec3(-45.0f, 30.0f, 0.0f); // 45° down, 30° yaw
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Point Light")) {
            Entity& e = m_scene.createEntity("Point Light");
            e.light = Light{LightType::Point};
            e.transform.position = m_scene.hasVisibleMeshes()
                ? m_scene.getSceneCenter() + glm::vec3(0, m_scene.getSceneSize().y, 0)
                : glm::vec3(0, 50, 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Spot Light")) {
            Entity& e = m_scene.createEntity("Spot Light");
            e.light = Light{LightType::Spot};
            e.transform.position = m_scene.hasVisibleMeshes()
                ? m_scene.getSceneCenter() + glm::vec3(0, m_scene.getSceneSize().y, 0)
                : glm::vec3(0, 50, 0);
        }

        // List all light entities with their type prefix; clicking selects them in the hierarchy
        ImGui::Separator();
        ImGui::Text("Scene Lights:");
        auto& entities = m_scene.getEntities();
        for (int i = 0; i < static_cast<int>(entities.size()); i++) {
            if (!entities[i].light.has_value()) continue;
            ImGui::PushID(i + 10000); // Offset ID to avoid collision with Scene Hierarchy IDs
            const char* typeLabel =
                entities[i].light->type == LightType::Directional ? "[Dir]" :
                entities[i].light->type == LightType::Point       ? "[Pt]"  : "[Sp]";
            char label[128];
            snprintf(label, sizeof(label), "%s %s", typeLabel, entities[i].name.c_str());
            if (ImGui::Selectable(label, m_selectedEntity == i))
                m_selectedEntity = i; // Cross-select: clicking a light here selects it in the hierarchy
            ImGui::PopID();
        }
    }
    ImGui::End();

    // =============================================
    // MATERIALS WINDOW
    // =============================================
    ImGui::Begin("Materials");
    {
        // Clamp selection in case the material list was modified since last frame
        int matCount = m_renderer.getMaterialManager().getMaterialCount();
        if (m_selectedMaterial >= matCount) m_selectedMaterial = 0;

        // Add a new material with neutral defaults and immediately select it
        if (ImGui::Button("Add Material")) {
            GPUMaterial newMat{};
            newMat.albedo    = glm::vec3(1.0f);
            newMat.metallic  = 0.0f;
            newMat.roughness = 0.5f;
            newMat.emissive  = 0.0f;
            newMat.textureID = -1;
            newMat.alpha     = 1.0f;  // Start fully opaque; user can reduce via slider
            m_selectedMaterial = static_cast<int>(
                m_renderer.getMaterialManager().addMaterial(newMat));
            m_renderer.getMaterialManager().upload();
        }

        ImGui::Separator();
        ImGui::Text("Materials (%d)", matCount);

        // Scrollable selectable list of material slots
        for (int i = 0; i < matCount; i++) {
            ImGui::PushID(i);
            char label[64];
            snprintf(label, sizeof(label), i == 0 ? "Material %d (default)" : "Material %d", i);
            if (ImGui::Selectable(label, m_selectedMaterial == i))
                m_selectedMaterial = i;
            ImGui::PopID();
        }

        // Inspector for the selected material slot
        if (m_selectedMaterial < matCount) {
            ImGui::Separator();
            // Copy → edit → write back pattern avoids partial updates to the GPU array
            GPUMaterial mat = m_renderer.getMaterialManager().getMaterial(
                static_cast<uint32_t>(m_selectedMaterial));

            bool changed = false;
            changed |= ImGui::ColorEdit3("Albedo",    &mat.albedo.x);
            changed |= ImGui::SliderFloat("Metallic",  &mat.metallic,  0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Emissive",  &mat.emissive,  0.0f, 10.0f);
            // Alpha: 1.0 = fully opaque (depth-tested). Values below 0.99 activate
            // Weighted Blended OIT — the fragment is accumulated into the transparent
            // layer and composited over the opaque image by pass5_oit_composite.comp.
            changed |= ImGui::SliderFloat("Alpha",     &mat.alpha,     0.0f, 1.0f);

            // Texture selector: "None" or one of the loaded texture layers
            {
                int texCount = m_renderer.getTextureManager().getTextureCount();
                std::string preview = "None";
                if (mat.textureID >= 0 && mat.textureID < texCount) {
                    const std::string& p = m_renderer.getTextureManager().getTexturePath(mat.textureID);
                    size_t slash = p.find_last_of("/\\");
                    preview = (slash != std::string::npos) ? p.substr(slash + 1) : p;
                }

                if (ImGui::BeginCombo("Texture", preview.c_str())) {
                    bool noneSelected = (mat.textureID < 0);
                    if (ImGui::Selectable("None", noneSelected)) { mat.textureID = -1; changed = true; }
                    if (noneSelected) ImGui::SetItemDefaultFocus();

                    for (int t = 0; t < texCount; t++) {
                        const std::string& p = m_renderer.getTextureManager().getTexturePath(t);
                        size_t slash = p.find_last_of("/\\");
                        std::string tname = (slash != std::string::npos) ? p.substr(slash + 1) : p;
                        bool sel = (mat.textureID == t);
                        if (ImGui::Selectable(tname.c_str(), sel)) { mat.textureID = t; changed = true; }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            if (changed) {
                m_renderer.getMaterialManager().setMaterial(
                    static_cast<uint32_t>(m_selectedMaterial), mat);
                m_renderer.getMaterialManager().upload();  // Push to GPU immediately
            }
        }
    }
    ImGui::End();

    // =============================================
    // TEXTURES WINDOW
    // =============================================
    ImGui::Begin("Textures");
    {
        int texCount = m_renderer.getTextureManager().getTextureCount();
        ImGui::Text("Textures (%d)", texCount);
        ImGui::Separator();

        if (ImGui::Button("Load Texture...")) {
            nfdu8char_t* outPath = nullptr;
            nfdu8filteritem_t filters[1] = { { "Image Files", "png,jpg,jpeg,tga,bmp" } };
            nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);
            if (result == NFD_OKAY && outPath) {
                int layerIdx = m_renderer.getTextureManager().addTexture(outPath);
                if (layerIdx >= 0)
                    m_renderer.getTextureManager().upload();
                NFD_FreePathU8(outPath);
            } else if (result == NFD_ERROR) {
                std::cerr << "[ERROR] NFD: " << NFD_GetError() << std::endl;
            }
        }

        ImGui::Separator();

        for (int i = 0; i < texCount; i++) {
            const std::string& path = m_renderer.getTextureManager().getTexturePath(i);
            size_t slash = path.find_last_of("/\\");
            std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            ImGui::Text("%d: %s", i, name.c_str());
        }
    }
    ImGui::End();

    // =============================================
    // RENDER (compute pipeline via Renderer)
    // =============================================
    // update() drains pending async GPU uploads and frees destroyed assets before rendering
    m_assetManager.update();
    m_renderer.render(m_scene, m_camera, m_assetManager);

    // =============================================
    // RENDER IMGUI TO DEFAULT FRAMEBUFFER
    // =============================================
    // ImGui draws its UI (including the viewport image) to the default framebuffer using OpenGL.
    // The viewport is first cleared to a near-black background, then ImGui draws on top.
    ImGui::Render();
    int fbW, fbH;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window); // Present the completed frame (VSync'd)
}

// ============================================
// SHUTDOWN
// ============================================

// Tear down all systems in reverse initialization order.
// ImGui must be shut down before the GL context is destroyed.
// GPU resources (renderer, assets) must be freed while the GL context is still current.
void Application::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // Release GPU resources before GL context is destroyed
    m_renderer.shutdown();
    m_assetManager.shutdown();

    NFD_Quit();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    std::cout << "[INFO] Engine shutdown complete" << std::endl;
}
