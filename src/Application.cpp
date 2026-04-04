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

bool Application::initialize() {
    // ------------------------------------------
    // 1. GLFW
    // ------------------------------------------
    if (!glfwInit()) {
        std::cerr << "[ERROR] Failed to initialize GLFW" << std::endl;
        return false;
    }

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
    glfwSwapInterval(1);

    m_inputManager.initialize(m_window);

    // ------------------------------------------
    // 2. GLAD
    // ------------------------------------------
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

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
    AssetHandle roomHandle = m_assetManager.load("models/room.off");
    if (roomHandle.isValid()) {
        Entity& e   = m_scene.createEntity("room");
        e.meshAsset = roomHandle;
        m_scene.recalculateSceneBounds(m_assetManager);
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
    bus().subscribe<WindowResizeEvent>([this](const WindowResizeEvent& e) {
        m_renderer.resize(e.width, e.height);
    });

    bus().subscribe<AssetLoadedEvent>([](const AssetLoadedEvent& e) {
        std::cout << "[EVENT] Asset loaded: " << e.path
                  << " (handle " << e.handle.id << ")" << std::endl;
    });

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

void Application::run() {
    while (!glfwWindowShouldClose(m_window)) {
        update();
    }
}

// ============================================
// UPDATE (one frame)
// ============================================

void Application::update() {
    // --- Timing ---
    float currentFrame = static_cast<float>(glfwGetTime());
    m_deltaTime = currentFrame - m_lastFrame;
    m_lastFrame = currentFrame;

    m_frameCount++;
    if (currentFrame - m_lastFPSTime >= 1.0) {
        m_currentFPS = static_cast<float>(m_frameCount);
        m_frameCount = 0;
        m_lastFPSTime = currentFrame;
    }

    glfwPollEvents();

    // --- ImGui frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    m_inputManager.update(io, m_viewportWasHovered);

    // --- Build initial dock layout once ---
    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport();
    if (m_firstLoop) {
        m_firstLoop = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImVec2 vpSize;
        vpSize.x = static_cast<float>(m_windowWidth);
        vpSize.y = static_cast<float>(m_windowHeight);
        ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

        ImGuiID dockLeft = 0, dockRight = 0, dockRightTop = 0, dockRightBottom = 0;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.75f, &dockLeft, &dockRight);
        ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.5f, &dockRightTop, &dockRightBottom);

        ImGui::DockBuilderDockWindow("###Viewport",       dockLeft);
        ImGui::DockBuilderDockWindow("Scene Hierarchy",   dockRightTop);
        ImGui::DockBuilderDockWindow("Asset Manager",     dockRightTop);
        ImGui::DockBuilderDockWindow("Camera Settings",   dockRightBottom);
        ImGui::DockBuilderDockWindow("Lighting",          dockRightBottom);
        ImGui::DockBuilderDockWindow("Mouse",             dockRightBottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    // =============================================
    // VIEWPORT WINDOW
    // =============================================
    char vpTitle[64];
    snprintf(vpTitle, sizeof(vpTitle), "Viewport  (%.0f FPS)###Viewport", m_currentFPS);
    ImGui::Begin(vpTitle);
    {
        // --- Resize detection ---
        ImVec2 avail = ImGui::GetContentRegionAvail();
        int newW = std::max(16, static_cast<int>(avail.x));
        int newH = std::max(16, static_cast<int>(avail.y));

        if (newW != m_renderer.getViewportWidth() || newH != m_renderer.getViewportHeight())
            bus().publish(WindowResizeEvent{newW, newH});

        // --- Camera input ---
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
                m_camera.processMouseMovement(dx, -dy);
        }

        // --- Camera rotate (Alt+LMB) ---
        if (m_inputManager.isLMBDown() && m_inputManager.isAltDown()
                && m_inputManager.isViewportHovered()) {
            float dx = m_inputManager.getAxis(Axis::MouseX);
            float dy = m_inputManager.getAxis(Axis::MouseY);
            if (dx != 0.0f || dy != 0.0f)
                m_camera.processMouseMovement(dx, -dy);
        }

        float scroll = m_inputManager.getAxis(Axis::MouseScrollY);
        if (m_inputManager.isViewportHovered() && scroll != 0.0f)
            m_camera.processMouseScroll(scroll);

        // --- Orbit (MMB drag) ---
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

        // --- Display compute output (flip V for OpenGL origin) ---
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
        if (ImGui::Button("Reset to Origin")) {
            m_camera.setPosition(glm::vec3(0.0f, 0.0f, 100.0f));
            m_camera.setYaw(-90.0f);
            m_camera.setPitch(0.0f);
            m_camera.setFOV(45.0f);
            m_camera.setMovementSpeed(50.0f);
            m_camera.setMouseSensitivity(0.1f);
        }

        // --- Bookmarks ---
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

        for (int i = 0; i < static_cast<int>(m_bookmarks.size()); i++) {
            ImGui::PushID(i);
            if (ImGui::Button("Load")) {
                const auto& b = m_bookmarks[i];
                m_camera.setPosition(b.position);
                m_camera.setOrientation(b.orientation);
                m_camera.setFOV(b.fov);
                m_camera.setMovementSpeed(b.movementSpeed);
                m_camera.setMouseSensitivity(b.mouseSensitivity);
            }
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                m_bookmarks.erase(m_bookmarks.begin() + i);
                ImGui::PopID();
                break;
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

        // Clamp selection in case an entity was deleted
        if (m_selectedEntity >= static_cast<int>(entities.size()))
            m_selectedEntity = -1;

        // --- Entity list ---
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

            // Name
            char nameBuf[256];
            strncpy_s(nameBuf, sizeof(nameBuf), ent.name.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                ent.name = nameBuf;

            // Transform
            bool changed = false;
            changed |= ImGui::DragFloat3("Position", &ent.transform.position.x, 1.0f);
            changed |= ImGui::DragFloat3("Rotation", &ent.transform.rotation.x, 1.0f);
            changed |= ImGui::DragFloat3("Scale",    &ent.transform.scale.x, 0.01f, 0.001f, 100.0f);
            changed |= ImGui::Checkbox("Visible", &ent.visible);

            if (changed) {
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }

            // Mesh info
            const MeshAsset* asset = m_assetManager.get(ent.meshAsset);
            if (asset) {
                ImGui::Text("Mesh: %s", std::filesystem::path(asset->filePath).filename().string().c_str());
                ImGui::Text("%zu verts, %zu faces",
                            asset->mesh.getVertexCount(), asset->mesh.getFaceCount());
            }

            ImGui::Separator();

            // Duplicate
            if (ImGui::Button("Duplicate")) {
                Entity clone      = ent;  // copy all fields
                clone.name        = ent.name + " (copy)";
                clone.transform.position += glm::vec3(50.0f, 0.0f, 0.0f);
                Entity& newEnt    = m_scene.createEntity(clone.name);
                newEnt.meshAsset  = clone.meshAsset;
                newEnt.transform  = clone.transform;
                newEnt.visible    = clone.visible;
                if (newEnt.meshAsset.isValid())
                    m_assetManager.addRef(newEnt.meshAsset);
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }
            ImGui::SameLine();

            // Delete
            if (ImGui::Button("Delete")) {
                AssetHandle handleToRelease = ent.meshAsset;
                uint32_t idToRemove = ent.id;
                m_scene.removeEntity(idToRemove);
                m_selectedEntity = -1;
                if (handleToRelease.isValid())
                    m_assetManager.release(handleToRelease);
                m_renderer.submitScene(m_scene, m_assetManager);
                m_scene.recalculateSceneBounds(m_assetManager);
            }

            // --- Light component ---
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

                if (light.type == LightType::Point || light.type == LightType::Spot)
                    ImGui::DragFloat("Range", &light.range, 1.0f, 1.0f, 10000.0f);

                if (light.type == LightType::Spot) {
                    ImGui::DragFloat("Inner Cone", &light.innerCone, 0.5f, 1.0f, 89.0f);
                    ImGui::DragFloat("Outer Cone", &light.outerCone, 0.5f, 1.0f, 90.0f);
                    if (light.outerCone < light.innerCone)
                        light.outerCone = light.innerCone + 1.0f;
                }

                if (ImGui::Button("Remove Light"))
                    ent.light.reset();
            } else {
                if (ImGui::Button("Add Light"))
                    ent.light = Light{};
            }
        }
    }
    ImGui::End();

    // =============================================
    // ASSET MANAGER WINDOW
    // =============================================
    ImGui::Begin("Asset Manager");
    {
        // --- Loaded mesh assets ---
        auto assetInfos = m_assetManager.getAssetInfos();

        if (!assetInfos.empty()) {
            ImGui::Text("Mesh Assets (%zu)", assetInfos.size());
            ImGui::Separator();
            for (const auto& info : assetInfos) {
                // Count entities referencing this asset
                int refCount = 0;
                for (const auto& e : m_scene.getEntities())
                    if (e.meshAsset == info.handle) refCount++;

                ImGui::Text("%s", std::filesystem::path(info.filePath).filename().string().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu v, %zu f, %d ref%s)",
                                    info.vertexCount,
                                    info.faceCount,
                                    refCount,
                                    info.ready ? "" : " [loading]");
            }
            ImGui::Separator();
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "No meshes loaded");
            ImGui::Separator();
        }

        // --- Import ---
        if (ImGui::Button("Import .off File")) {
            nfdu8char_t* outPath = nullptr;
            nfdu8filteritem_t filters[1] = { { "OFF Files", "off" } };
            nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);

            if (result == NFD_OKAY) {
                std::string newPath(outPath);
                NFD_FreePathU8(outPath);

                AssetHandle handle = m_assetManager.load(newPath);
                if (handle.isValid()) {
                    std::string entityName =
                        std::filesystem::path(newPath).stem().string();
                    Entity& e    = m_scene.createEntity(entityName);
                    e.meshAsset  = handle;
                    m_scene.recalculateSceneBounds(m_assetManager);
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
        const char* modelNames[] = { "Blinn-Phong", "PBR (Metallic-Roughness)" };
        int modelIdx = static_cast<int>(m_renderer.getLightManager().getShadingModel());
        if (ImGui::Combo("Shading Model", &modelIdx, modelNames, 2))
            m_renderer.getLightManager().setShadingModel(static_cast<ShadingModel>(modelIdx));

        ImGui::Separator();

        if (ImGui::Button("Add Directional Light")) {
            Entity& e = m_scene.createEntity("Directional Light");
            e.light = Light{LightType::Directional};
            e.transform.rotation = glm::vec3(-45.0f, 30.0f, 0.0f);
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

        ImGui::Separator();
        ImGui::Text("Scene Lights:");
        auto& entities = m_scene.getEntities();
        for (int i = 0; i < static_cast<int>(entities.size()); i++) {
            if (!entities[i].light.has_value()) continue;
            ImGui::PushID(i + 10000);
            const char* typeLabel =
                entities[i].light->type == LightType::Directional ? "[Dir]" :
                entities[i].light->type == LightType::Point       ? "[Pt]"  : "[Sp]";
            char label[128];
            snprintf(label, sizeof(label), "%s %s", typeLabel, entities[i].name.c_str());
            if (ImGui::Selectable(label, m_selectedEntity == i))
                m_selectedEntity = i;
            ImGui::PopID();
        }
    }
    ImGui::End();

    // =============================================
    // RENDER (compute pipeline via Renderer)
    // =============================================
    m_assetManager.update();
    m_renderer.render(m_scene, m_camera, m_assetManager);

    // =============================================
    // RENDER IMGUI TO DEFAULT FRAMEBUFFER
    // =============================================
    ImGui::Render();
    int fbW, fbH;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
}

// ============================================
// SHUTDOWN
// ============================================

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
