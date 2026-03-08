#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.h>

#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdint>

#include "ComputeShader.h"
#include "COFFParser.h"
#include "GPUBuffer.h"
#include "Mesh.h"
#include "Camera.h"
#include "CameraState.h"
#include "TileRasterizer.h"
#include "TileData.h"

// ============================================
// GLOBALS
// ============================================
const int WINDOW_WIDTH  = 1600;
const int WINDOW_HEIGHT = 900;

glm::vec3 meshCenter(0.0f);
glm::vec3 meshSize(0.0f);
glm::vec3 meshMinBounds(0.0f);
glm::vec3 meshMaxBounds(0.0f);

// ============================================
// HELPER FUNCTIONS
// ============================================

GLuint createOutputTexture(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint createDepthBuffer(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void calculateMeshBounds(const Mesh& mesh) {
    meshMinBounds = glm::vec3(1e10f);
    meshMaxBounds = glm::vec3(-1e10f);
    for (const auto& v : mesh.vertices) {
        meshMinBounds = glm::min(meshMinBounds, v.position);
        meshMaxBounds = glm::max(meshMaxBounds, v.position);
    }
    meshCenter = (meshMinBounds + meshMaxBounds) * 0.5f;
    meshSize   = meshMaxBounds - meshMinBounds;
}

// ============================================
// MAIN
// ============================================
int main() {
    // ------------------------------------------
    // 1. GLFW
    // ------------------------------------------
    if (!glfwInit()) {
        std::cerr << "[ERROR] Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                          "Triangle Splat Engine", nullptr, nullptr);
    if (!window) {
        std::cerr << "[ERROR] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ------------------------------------------
    // 2. GLAD
    // ------------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[ERROR] Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
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
    ImGui_ImplGlfw_InitForOpenGL(window, true);   // installs GLFW callbacks
    ImGui_ImplOpenGL3_Init("#version 430");

    // ------------------------------------------
    // 4. NATIVE FILE DIALOG
    // ------------------------------------------
    if (NFD_Init() != NFD_OKAY) {
        std::cerr << "[WARNING] NFD init failed — file dialogs disabled" << std::endl;
    }

    // ------------------------------------------
    // 5. COMPUTE SHADERS
    // ------------------------------------------
    ComputeShader clearDepthShader("shaders/clear_depth.comp");
    ComputeShader clearTilesShader("shaders/clear_tiles.comp");
    ComputeShader pass1Shader("shaders/pass1_count.comp");
    ComputeShader pass2Shader("shaders/pass2_prefix_sum.comp");
    ComputeShader pass3Shader("shaders/pass3_bin.comp");
    ComputeShader pass4Shader("shaders/pass4_rasterize_tiled.comp");
    ComputeShader debugTilesShader("shaders/debug_tiles.comp");

    if (!clearDepthShader.isValid() || !clearTilesShader.isValid() ||
        !pass1Shader.isValid()      || !pass2Shader.isValid()      ||
        !pass3Shader.isValid()      || !pass4Shader.isValid()      ||
        !debugTilesShader.isValid()) {
        std::cerr << "[ERROR] One or more compute shaders failed to compile" << std::endl;
        glfwTerminate();
        return -1;
    }

    // ------------------------------------------
    // 6. VIEWPORT RESOURCES (initial size)
    // ------------------------------------------
    int  viewportWidth  = 256;
    int  viewportHeight = 256;
    GLuint outputTexture = createOutputTexture(viewportWidth, viewportHeight);
    GLuint depthBuffer   = createDepthBuffer(viewportWidth, viewportHeight);

    // ------------------------------------------
    // 7. TILE RASTERIZER (minimal until mesh loaded)
    // ------------------------------------------
    TileRasterizer tileRasterizer(viewportWidth, viewportHeight);
    tileRasterizer.initialize(1);

    // ------------------------------------------
    // 8. TRY LOADING DEFAULT MESH
    // ------------------------------------------
    Camera    camera(glm::vec3(0.0f, 0.0f, 100.0f));
    Mesh      currentMesh;
    GPUBuffer gpuBuffer;
    bool      meshLoaded = false;
    std::string loadedMeshPath;

    if (COFFParser::loadFromFile("models/room.off", currentMesh) &&
        currentMesh.getVertexCount() > 0 && currentMesh.getFaceCount() > 0) {

        gpuBuffer.uploadMesh(currentMesh);
        calculateMeshBounds(currentMesh);
        tileRasterizer.resize(viewportWidth, viewportHeight, currentMesh.getFaceCount());

        camera.setPosition(meshCenter + glm::vec3(0.0f, meshSize.y * 0.5f, meshSize.z * 2.0f));
        camera.lookAt(meshCenter);

        loadedMeshPath = "models/room.off";
        meshLoaded = true;
        std::cout << "[INFO] Default mesh loaded: " << currentMesh.getVertexCount()
                  << " verts, " << currentMesh.getFaceCount() << " faces" << std::endl;
    } else {
        std::cout << "[INFO] No default mesh found — use Asset Manager to import" << std::endl;
    }

    // ------------------------------------------
    // 9. EDITOR STATE
    // ------------------------------------------
    std::vector<CameraState> bookmarks;
    bool  debugMode = false;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    int   frameCount = 0;
    double lastFPSTime = glfwGetTime();
    float  currentFPS  = 0.0f;

    std::cout << "[INFO] Editor ready — hold Right-Click in Viewport for camera control" << std::endl;

    // ==========================================
    // RENDER LOOP
    // ==========================================
    while (!glfwWindowShouldClose(window)) {
        // --- Timing ---
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        frameCount++;
        if (currentFrame - lastFPSTime >= 1.0) {
            currentFPS  = static_cast<float>(frameCount);
            frameCount  = 0;
            lastFPSTime = currentFrame;
        }

        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        // --- ImGui frame ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport();

        // =============================================
        // VIEWPORT WINDOW
        // =============================================
        char vpTitle[64];
        snprintf(vpTitle, sizeof(vpTitle), "Viewport  (%.0f FPS)###Viewport", currentFPS);
        ImGui::Begin(vpTitle);
        {
            // --- Resize detection ---
            ImVec2 avail = ImGui::GetContentRegionAvail();
            int newW = std::max(16, static_cast<int>(avail.x));
            int newH = std::max(16, static_cast<int>(avail.y));

            if (newW != viewportWidth || newH != viewportHeight) {
                glFinish();
                glDeleteTextures(1, &outputTexture);
                glDeleteTextures(1, &depthBuffer);
                viewportWidth  = newW;
                viewportHeight = newH;
                outputTexture = createOutputTexture(viewportWidth, viewportHeight);
                depthBuffer   = createDepthBuffer(viewportWidth, viewportHeight);
                if (meshLoaded) {
                    tileRasterizer.resize(viewportWidth, viewportHeight,
                                         currentMesh.getFaceCount());
                }
            }

            // --- Camera input (RMB + hover) ---
            bool vpHovered = ImGui::IsWindowHovered();
            bool rmbDown   = ImGui::IsMouseDown(ImGuiMouseButton_Right);

            if (vpHovered && rmbDown) {
                if (ImGui::IsKeyDown(ImGuiKey_W))
                    camera.processKeyboard(CameraMovement::FORWARD, deltaTime);
                if (ImGui::IsKeyDown(ImGuiKey_S))
                    camera.processKeyboard(CameraMovement::BACKWARD, deltaTime);
                if (ImGui::IsKeyDown(ImGuiKey_A))
                    camera.processKeyboard(CameraMovement::LEFT, deltaTime);
                if (ImGui::IsKeyDown(ImGuiKey_D))
                    camera.processKeyboard(CameraMovement::RIGHT, deltaTime);
                if (ImGui::IsKeyDown(ImGuiKey_Space))
                    camera.processKeyboard(CameraMovement::UP, deltaTime);
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
                    camera.processKeyboard(CameraMovement::DOWN, deltaTime);

                ImVec2 delta = io.MouseDelta;
                if (delta.x != 0.0f || delta.y != 0.0f)
                    camera.processMouseMovement(delta.x, -delta.y);
            }

            if (vpHovered && io.MouseWheel != 0.0f)
                camera.processMouseScroll(io.MouseWheel);

            // --- Display compute output (flip V for OpenGL origin) ---
            ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(outputTexture)),
                         ImVec2(static_cast<float>(viewportWidth),
                                static_cast<float>(viewportHeight)),
                         ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::End();

        // =============================================
        // CAMERA SETTINGS WINDOW
        // =============================================
        ImGui::Begin("Camera Settings");
        {
            glm::vec3 pos = camera.getPosition();
            if (ImGui::DragFloat3("Position", &pos.x, 1.0f))
                camera.setPosition(pos);

            float yaw = camera.getYaw();
            if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f))
                camera.setYaw(yaw);

            float pitch = camera.getPitch();
            if (ImGui::SliderFloat("Pitch", &pitch, -89.0f, 89.0f))
                camera.setPitch(pitch);

            float fov = camera.getFOV();
            if (ImGui::SliderFloat("FOV", &fov, 1.0f, 90.0f))
                camera.setFOV(fov);

            float speed = camera.getMovementSpeed();
            if (ImGui::DragFloat("Speed", &speed, 1.0f, 1.0f, 1000.0f))
                camera.setMovementSpeed(speed);

            float sens = camera.getMouseSensitivity();
            if (ImGui::DragFloat("Sensitivity", &sens, 0.01f, 0.01f, 1.0f))
                camera.setMouseSensitivity(sens);

            // --- Presets ---
            ImGui::Separator();
            ImGui::Text("Presets");

            if (ImGui::Button("Top-Down View")) {
                if (meshLoaded) {
                    camera.setPosition(glm::vec3(meshCenter.x,
                                                 meshMaxBounds.y + meshSize.y,
                                                 meshCenter.z));
                    camera.lookAt(meshCenter);
                } else {
                    camera.setPosition(glm::vec3(0.0f, 500.0f, 0.0f));
                    camera.setPitch(-89.0f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to Origin")) {
                camera.setPosition(glm::vec3(0.0f, 0.0f, 100.0f));
                camera.setYaw(-90.0f);
                camera.setPitch(0.0f);
                camera.setFOV(45.0f);
                camera.setMovementSpeed(50.0f);
                camera.setMouseSensitivity(0.1f);
            }

            // --- Bookmarks ---
            ImGui::Separator();
            ImGui::Text("Bookmarks");

            if (ImGui::Button("Save Current State")) {
                CameraState s;
                s.name            = "Bookmark " + std::to_string(bookmarks.size() + 1);
                s.position        = camera.getPosition();
                s.yaw             = camera.getYaw();
                s.pitch           = camera.getPitch();
                s.fov             = camera.getFOV();
                s.movementSpeed   = camera.getMovementSpeed();
                s.mouseSensitivity = camera.getMouseSensitivity();
                bookmarks.push_back(s);
            }

            for (int i = 0; i < static_cast<int>(bookmarks.size()); i++) {
                ImGui::PushID(i);
                if (ImGui::Button("Load")) {
                    const auto& b = bookmarks[i];
                    camera.setPosition(b.position);
                    camera.setYaw(b.yaw);
                    camera.setPitch(b.pitch);
                    camera.setFOV(b.fov);
                    camera.setMovementSpeed(b.movementSpeed);
                    camera.setMouseSensitivity(b.mouseSensitivity);
                }
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    bookmarks.erase(bookmarks.begin() + i);
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(bookmarks[i].name.c_str());
                ImGui::PopID();
            }
        }
        ImGui::End();

        // =============================================
        // ASSET MANAGER WINDOW
        // =============================================
        ImGui::Begin("Asset Manager");
        {
            if (meshLoaded) {
                ImGui::Text("File: %s", loadedMeshPath.c_str());
                ImGui::Text("Vertices: %zu   Faces: %zu",
                            currentMesh.getVertexCount(), currentMesh.getFaceCount());
                ImGui::Text("Bounds: (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)",
                            meshMinBounds.x, meshMinBounds.y, meshMinBounds.z,
                            meshMaxBounds.x, meshMaxBounds.y, meshMaxBounds.z);
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "No mesh loaded");
            }

            ImGui::Separator();

            if (ImGui::Button("Import .off File")) {
                nfdu8char_t* outPath = nullptr;
                nfdu8filteritem_t filters[1] = { { "OFF Files", "off" } };
                nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);

                if (result == NFD_OKAY) {
                    std::string newPath(outPath);
                    NFD_FreePathU8(outPath);

                    Mesh newMesh;
                    if (COFFParser::loadFromFile(newPath, newMesh) &&
                        newMesh.getVertexCount() > 0 && newMesh.getFaceCount() > 0) {

                        glFinish();
                        currentMesh = std::move(newMesh);
                        gpuBuffer.uploadMesh(currentMesh);
                        calculateMeshBounds(currentMesh);
                        tileRasterizer.resize(viewportWidth, viewportHeight,
                                              currentMesh.getFaceCount());

                        camera.setPosition(meshCenter +
                                           glm::vec3(0.0f, meshSize.y * 0.5f, meshSize.z * 2.0f));
                        camera.lookAt(meshCenter);

                        loadedMeshPath = newPath;
                        meshLoaded = true;
                        std::cout << "[INFO] Imported: " << newPath << " ("
                                  << currentMesh.getVertexCount() << " verts, "
                                  << currentMesh.getFaceCount() << " faces)" << std::endl;
                    } else {
                        std::cerr << "[ERROR] Failed to parse: " << newPath << std::endl;
                    }
                } else if (result == NFD_ERROR) {
                    std::cerr << "[ERROR] NFD: " << NFD_GetError() << std::endl;
                }
            }

            ImGui::SameLine();
            ImGui::Checkbox("Debug Tiles", &debugMode);
        }
        ImGui::End();

        // =============================================
        // COMPUTE PIPELINE (Phases 0-3)
        // =============================================
        if (meshLoaded && viewportWidth > 0 && viewportHeight > 0) {
            GLuint numGroupsX = (viewportWidth  + 15) / 16;
            GLuint numGroupsY = (viewportHeight + 15) / 16;
            float  aspectRatio = static_cast<float>(viewportWidth) /
                                 static_cast<float>(viewportHeight);
            glm::mat4 viewProjection =
                camera.getViewProjectionMatrix(aspectRatio, camera.getFOV());

            // --- Phase 0: Clear ---
            clearDepthShader.use();
            glBindImageTexture(0, depthBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
            clearDepthShader.dispatch(numGroupsX, numGroupsY, 1);

            clearTilesShader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                             tileRasterizer.getTileCounterBufferID());
            clearTilesShader.setUInt("numElements",
                                    static_cast<GLuint>(tileRasterizer.getTotalTiles()));
            GLuint numTileGroups = (tileRasterizer.getTotalTiles() + 255) / 256;
            clearTilesShader.dispatch(numTileGroups, 1, 1);

            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            // --- Phase 1: Project & Count ---
            pass1Shader.use();
            gpuBuffer.bindVertexBuffer(0);
            gpuBuffer.bindFaceBuffer(1);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                             tileRasterizer.getProjectedTriangleBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,
                             tileRasterizer.getTileCounterBufferID());

            pass1Shader.setMat4("viewProjection", viewProjection);
            pass1Shader.setIVec2("screenSize",
                                 glm::ivec2(viewportWidth, viewportHeight));
            pass1Shader.setIVec2("numTiles",
                                 glm::ivec2(tileRasterizer.getNumTilesX(),
                                            tileRasterizer.getNumTilesY()));

            GLuint numTriangleGroups =
                (static_cast<GLuint>(currentMesh.getFaceCount()) + 255) / 256;
            pass1Shader.dispatch(numTriangleGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // --- Phase 1.5: Prefix Sum ---
            pass2Shader.use();
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                             tileRasterizer.getTileCounterBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                             tileRasterizer.getTileOffsetBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                             tileRasterizer.getBlockSumBufferID());

            GLuint numScanGroups = (tileRasterizer.getTotalTiles() + 255) / 256;

            pass2Shader.setUInt("numElements",
                                static_cast<GLuint>(tileRasterizer.getTotalTiles()));
            pass2Shader.setUInt("pass", 0u);
            pass2Shader.dispatch(numScanGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            if (numScanGroups > 1) {
                pass2Shader.setUInt("numElements",
                                    static_cast<GLuint>(numScanGroups));
                pass2Shader.setUInt("pass", 1u);
                pass2Shader.dispatch(1, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }

            pass2Shader.setUInt("numElements",
                                static_cast<GLuint>(tileRasterizer.getTotalTiles()));
            pass2Shader.setUInt("pass", 2u);
            pass2Shader.dispatch(numScanGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

            size_t totalPairs = tileRasterizer.pass2_prefixSum();

            // --- Phase 2: Bin ---
            if (totalPairs > 0) {
                tileRasterizer.pass3_binTriangles(currentMesh.getFaceCount());

                pass3Shader.use();
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                                 tileRasterizer.getProjectedTriangleBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,
                                 tileRasterizer.getTileCounterBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,
                                 tileRasterizer.getTileOffsetBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,
                                 tileRasterizer.getTriangleListBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,
                                 tileRasterizer.getTileWriteOffsetBufferID());

                pass3Shader.setIVec2("numTiles",
                                     glm::ivec2(tileRasterizer.getNumTilesX(),
                                                tileRasterizer.getNumTilesY()));
                pass3Shader.setUInt("maxTriangleListSize",
                                    static_cast<GLuint>(
                                        tileRasterizer.getTotalTriangleTilePairs()));
                pass3Shader.dispatch(numTriangleGroups, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }

            // --- Phase 3: Rasterize ---
            if (debugMode) {
                debugTilesShader.use();
                glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0,
                                   GL_WRITE_ONLY, GL_RGBA32F);
                debugTilesShader.setIVec2("screenSize",
                                         glm::ivec2(viewportWidth, viewportHeight));
                debugTilesShader.dispatch(numGroupsX, numGroupsY, 1);
            } else if (totalPairs > 0) {
                pass4Shader.use();
                glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0,
                                   GL_WRITE_ONLY, GL_RGBA32F);
                glBindImageTexture(1, depthBuffer, 0, GL_FALSE, 0,
                                   GL_READ_WRITE, GL_R32UI);
                gpuBuffer.bindVertexBuffer(2);
                gpuBuffer.bindFaceBuffer(3);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,
                                 tileRasterizer.getProjectedTriangleBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5,
                                 tileRasterizer.getTileOffsetBufferID());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6,
                                 tileRasterizer.getTriangleListBufferID());

                pass4Shader.setIVec2("screenSize",
                                     glm::ivec2(viewportWidth, viewportHeight));
                pass4Shader.setIVec2("numTiles",
                                     glm::ivec2(tileRasterizer.getNumTilesX(),
                                                tileRasterizer.getNumTilesY()));
                pass4Shader.dispatch(numGroupsX, numGroupsY, 1);
            } else {
                // No visible geometry — fill with dark background
                glBindTexture(GL_TEXTURE_2D, outputTexture);
                std::vector<float> bg(viewportWidth * viewportHeight * 4);
                for (int i = 0; i < viewportWidth * viewportHeight; i++) {
                    bg[i * 4 + 0] = 0.1f;
                    bg[i * 4 + 1] = 0.1f;
                    bg[i * 4 + 2] = 0.1f;
                    bg[i * 4 + 3] = 1.0f;
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                viewportWidth, viewportHeight,
                                GL_RGBA, GL_FLOAT, bg.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        // Ensure compute writes are visible before ImGui samples the texture
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // =============================================
        // RENDER IMGUI TO DEFAULT FRAMEBUFFER
        // =============================================
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ==========================================
    // CLEANUP
    // ==========================================
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteTextures(1, &outputTexture);
    glDeleteTextures(1, &depthBuffer);

    NFD_Quit();

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[INFO] Engine shutdown complete" << std::endl;

    return 0;
}
