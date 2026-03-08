#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include "ComputeShader.h"
#include "Shader.h"
#include "COFFParser.h"
#include "GPUBuffer.h"
#include "Mesh.h"
#include "Camera.h"
#include "TileRasterizer.h"
#include "TileData.h"

// Mesh bounds (global for keyboard callbacks)
glm::vec3 meshCenter(0.0f);
glm::vec3 meshSize(0.0f);
glm::vec3 meshMinBounds(0.0f);
glm::vec3 meshMaxBounds(0.0f);

// Window dimensions
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

// Camera
Camera camera(glm::vec3(0.0f, 0.0f, 100.0f));

// Mouse state
bool firstMouse = true;
float lastX = WINDOW_WIDTH / 2.0f;
float lastY = WINDOW_HEIGHT / 2.0f;

// Debug mode
bool debugMode = false;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Callback: Adjusts OpenGL viewport when window is resized
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// Callback: Handles keyboard input
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        glm::vec3 pos = camera.getPosition();
        glm::vec3 front = camera.getFront();
        extern glm::vec3 meshCenter, meshSize, meshMinBounds, meshMaxBounds;

        std::cout << "\n========== CAMERA & MESH INFO ==========" << std::endl;
        std::cout << "[CAMERA] Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        std::cout << "[CAMERA] Looking at: (" << front.x << ", " << front.y << ", " << front.z << ")" << std::endl;
        std::cout << "[CAMERA] FOV: " << camera.getFOV() << " degrees" << std::endl;
        std::cout << "[MESH] Center: (" << meshCenter.x << ", " << meshCenter.y << ", " << meshCenter.z << ")" << std::endl;
        std::cout << "[MESH] Size: (" << meshSize.x << ", " << meshSize.y << ", " << meshSize.z << ")" << std::endl;
        std::cout << "[MESH] Min: (" << meshMinBounds.x << ", " << meshMinBounds.y << ", " << meshMinBounds.z << ")" << std::endl;
        std::cout << "[MESH] Max: (" << meshMaxBounds.x << ", " << meshMaxBounds.y << ", " << meshMaxBounds.z << ")" << std::endl;

        float distToCenter = glm::length(pos - meshCenter);
        std::cout << "[DISTANCE] Camera to mesh center: " << distToCenter << " units" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

    if (key == GLFW_KEY_1 && action == GLFW_PRESS) {
        extern glm::vec3 meshCenter, meshSize, meshMaxBounds, meshMinBounds;
        glm::vec3 pos = glm::vec3(meshCenter.x, meshCenter.y + meshSize.y, meshCenter.z + glm::max(glm::max(meshSize.x, meshSize.y), meshSize.z) * 2.0f);
        camera.setPosition(pos);
        camera.lookAt(meshCenter);
        std::cout << "\n[CAMERA] Moved to Position 1 (Far view): (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        std::cout << "[CAMERA] Now looking at mesh center\n" << std::endl;
    }

    if (key == GLFW_KEY_2 && action == GLFW_PRESS) {
        extern glm::vec3 meshCenter, meshSize;
        glm::vec3 pos = glm::vec3(meshCenter.x, meshCenter.y, meshCenter.z + meshSize.z * 0.5f);
        camera.setPosition(pos);
        camera.lookAt(meshCenter);
        std::cout << "\n[CAMERA] Moved to Position 2 (Close front): (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        std::cout << "[CAMERA] Now looking at mesh center\n" << std::endl;
    }

    if (key == GLFW_KEY_3 && action == GLFW_PRESS) {
        extern glm::vec3 meshCenter, meshSize, meshMaxBounds;
        glm::vec3 pos = glm::vec3(meshCenter.x, meshMaxBounds.y + meshSize.y, meshCenter.z);
        camera.setPosition(pos);
        camera.lookAt(meshCenter);
        std::cout << "\n[CAMERA] Moved to Position 3 (Top-down): (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        std::cout << "[CAMERA] Now looking at mesh center\n" << std::endl;
    }

    if (key == GLFW_KEY_0 && action == GLFW_PRESS) {
        debugMode = !debugMode;
        std::cout << "[DEBUG] Debug visualization: " << (debugMode ? "ON" : "OFF") << std::endl;
    }

    if (key == GLFW_KEY_4 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(0.0f, 0.0f, -500.0f));
        std::cout << "[CAMERA] Moved to Position 4 (Behind, looking forward)" << std::endl;
    }

    if (key == GLFW_KEY_5 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(500.0f, 0.0f, 0.0f));
        std::cout << "[CAMERA] Moved to Position 5 (Right side)" << std::endl;
    }

    if (key == GLFW_KEY_6 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(0.0f, 500.0f, 0.0f));
        std::cout << "[CAMERA] Moved to Position 6 (High above)" << std::endl;
    }

    if (key == GLFW_KEY_7 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(-500.0f, 0.0f, 0.0f));
        std::cout << "[CAMERA] Moved to Position 7 (Left side)" << std::endl;
    }

    if (key == GLFW_KEY_8 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(0.0f, -500.0f, 0.0f));
        std::cout << "[CAMERA] Moved to Position 8 (Below)" << std::endl;
    }

    if (key == GLFW_KEY_9 && action == GLFW_PRESS) {
        camera.setPosition(glm::vec3(0.0f, 0.0f, 500.0f));
        std::cout << "[CAMERA] Moved to Position 9 (Front, looking back)" << std::endl;
    }
}

// Callback: Handles mouse movement
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xOffset = static_cast<float>(xpos) - lastX;
    float yOffset = lastY - static_cast<float>(ypos);

    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    camera.processMouseMovement(xOffset, yOffset);
}

// Callback: Handles mouse scroll
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.processMouseScroll(static_cast<float>(yoffset));
}

// Process continuous keyboard input
void processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.processKeyboard(CameraMovement::DOWN, deltaTime);
}

// Creates a fullscreen quad (two triangles covering NDC space -1 to 1)
GLuint createFullscreenQuad() {
    // Vertices: Position (x, y) and TexCoords (u, v)
    float quadVertices[] = {
        // Positions   // TexCoords
        -1.0f,  1.0f,  0.0f, 1.0f,  // Top-left
        -1.0f, -1.0f,  0.0f, 0.0f,  // Bottom-left
         1.0f, -1.0f,  1.0f, 0.0f,  // Bottom-right

        -1.0f,  1.0f,  0.0f, 1.0f,  // Top-left
         1.0f, -1.0f,  1.0f, 0.0f,  // Bottom-right
         1.0f,  1.0f,  1.0f, 1.0f   // Top-right
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // TexCoord attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    std::cout << "[INFO] Fullscreen quad created (VAO: " << VAO << ")" << std::endl;
    return VAO;
}

// Creates a 2D texture for compute shader output
GLuint createOutputTexture(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Allocate texture storage (RGBA32F = 4 channels, 32-bit float per channel)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[INFO] Output texture created: " << width << "x" << height 
              << " (RGBA32F, " << (width * height * 16 / 1024 / 1024) << " MB)" << std::endl;
    return texture;
}

// Creates a depth buffer for atomic depth testing
GLuint createDepthBuffer(int width, int height) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // R32UI format required for imageAtomicMin
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[INFO] Depth buffer created: " << width << "x" << height 
              << " (R32UI, " << (width * height * 4 / 1024 / 1024) << " MB)" << std::endl;
    return texture;
}

// Clears the depth buffer to maximum value
void clearDepthBuffer(GLuint depthBuffer, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, depthBuffer);

    std::vector<GLuint> clearData(width * height, 0xFFFFFFFF);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_INT, clearData.data());

    glBindTexture(GL_TEXTURE_2D, 0);
}

int main() {
    // ============================================
    // 1. INITIALIZE GLFW
    // ============================================
    if (!glfwInit()) {
        std::cerr << "[ERROR] Failed to initialize GLFW" << std::endl;
        return -1;
    }
    std::cout << "[INFO] GLFW initialized successfully" << std::endl;

    // ============================================
    // 2. CONFIGURE OPENGL CONTEXT
    // ============================================
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // ============================================
    // 3. CREATE WINDOW
    // ============================================
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Triangle Splat Engine", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "[ERROR] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "[INFO] Window created (" << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << ")" << std::endl;

    glfwMakeContextCurrent(window);

    // ============================================
    // 4. INITIALIZE GLAD
    // ============================================
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "[ERROR] Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "[INFO] GLAD initialized (OpenGL functions loaded)" << std::endl;
    std::cout << "[INFO] OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "[INFO] GPU: " << glGetString(GL_RENDERER) << std::endl;

    // ============================================
    // 5. CONFIGURE VIEWPORT & CALLBACKS
    // ============================================
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwSwapInterval(1);  // Enable VSync

    // ============================================
    // 6. CREATE OUTPUT TEXTURE (Canvas for compute shader)
    // ============================================
    GLuint outputTexture = createOutputTexture(WINDOW_WIDTH, WINDOW_HEIGHT);
    GLuint depthBuffer = createDepthBuffer(WINDOW_WIDTH, WINDOW_HEIGHT);

    // ============================================
    // 7. LOAD SHADERS
    // ============================================
    ComputeShader clearDepthShader("shaders/clear_depth.comp");
    if (!clearDepthShader.isValid()) {
        std::cerr << "[ERROR] Failed to load depth clear shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader clearTilesShader("shaders/clear_tiles.comp");
    if (!clearTilesShader.isValid()) {
        std::cerr << "[ERROR] Failed to load tiles clear shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader pass1Shader("shaders/pass1_count.comp");
    if (!pass1Shader.isValid()) {
        std::cerr << "[ERROR] Failed to load pass1 shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader pass2Shader("shaders/pass2_prefix_sum.comp");
    if (!pass2Shader.isValid()) {
        std::cerr << "[ERROR] Failed to load pass2 shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader pass3Shader("shaders/pass3_bin.comp");
    if (!pass3Shader.isValid()) {
        std::cerr << "[ERROR] Failed to load pass3 shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader pass4Shader("shaders/pass4_rasterize_tiled.comp");
    if (!pass4Shader.isValid()) {
        std::cerr << "[ERROR] Failed to load pass4 shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader debugTilesShader("shaders/debug_tiles.comp");
    if (!debugTilesShader.isValid()) {
        std::cerr << "[ERROR] Failed to load debug tiles shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    ComputeShader computeShader("shaders/rasterize.comp");
    if (!computeShader.isValid()) {
        std::cerr << "[ERROR] Failed to load compute shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    Shader displayShader("shaders/fullscreen.vert", "shaders/fullscreen.frag");
    if (!displayShader.isValid()) {
        std::cerr << "[ERROR] Failed to load display shader" << std::endl;
        glfwTerminate();
        return -1;
    }

    // ============================================
    // 8. CREATE FULLSCREEN QUAD
    // ============================================
    GLuint quadVAO = createFullscreenQuad();

    // Configure display shader to use texture unit 0
    displayShader.use();
    displayShader.setInt("screenTexture", 0);

    std::cout << "[INFO] Entering render loop (Press ESC to exit)" << std::endl;
    std::cout << "[INFO] Rendering compute shader output to screen..." << std::endl;

    // ============================================
    // TEST: Load COFF File and Upload to GPU
    // ============================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "LOADING 3D MESH FROM COFF FILE" << std::endl;
    std::cout << "========================================\n" << std::endl;

    Mesh testMesh;
    if (!COFFParser::loadFromFile("models/room.off", testMesh)) {
        std::cerr << "[ERROR] Failed to load mesh" << std::endl;
        glfwTerminate();
        return -1;
    }

    std::cout << "[INFO] Mesh loaded: " << testMesh.getVertexCount() << " vertices, " 
              << testMesh.getFaceCount() << " faces" << std::endl;

    if (testMesh.getVertexCount() == 0 || testMesh.getFaceCount() == 0) {
        std::cerr << "[ERROR] Mesh is empty! File might be corrupt." << std::endl;
        glfwTerminate();
        return -1;
    }

    // Calculate mesh bounds
    glm::vec3 minBounds(1e10f);
    glm::vec3 maxBounds(-1e10f);
    for (const auto& vertex : testMesh.vertices) {
        minBounds = glm::min(minBounds, vertex.position);
        maxBounds = glm::max(maxBounds, vertex.position);
    }
    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    glm::vec3 size = maxBounds - minBounds;

    meshCenter = center;
    meshSize = size;
    meshMinBounds = minBounds;
    meshMaxBounds = maxBounds;

    std::cout << "[INFO] Mesh bounds:" << std::endl;
    std::cout << "       Min: (" << minBounds.x << ", " << minBounds.y << ", " << minBounds.z << ")" << std::endl;
    std::cout << "       Max: (" << maxBounds.x << ", " << maxBounds.y << ", " << maxBounds.z << ")" << std::endl;
    std::cout << "       Center: (" << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;
    std::cout << "       Size: (" << size.x << ", " << size.y << ", " << size.z << ")" << std::endl;

    camera.setPosition(center + glm::vec3(0.0f, size.y * 0.5f, size.z * 2.0f));
    camera.lookAt(center);

    std::cout << "[INFO] Camera positioned at: (" << camera.getPosition().x << ", " 
              << camera.getPosition().y << ", " << camera.getPosition().z << ")" << std::endl;
    std::cout << "[INFO] Camera now looking at mesh center!" << std::endl;

    glm::mat4 testViewProj = camera.getViewProjectionMatrix(static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 45.0f);
    glm::vec3 testVertex = testMesh.vertices[0].position;
    glm::vec4 clipSpace = testViewProj * glm::vec4(testVertex, 1.0f);

    std::cout << "\n[CPU PROJECTION TEST]" << std::endl;
    std::cout << "  Test vertex (world): (" << testVertex.x << ", " << testVertex.y << ", " << testVertex.z << ")" << std::endl;
    std::cout << "  Clip space: (" << clipSpace.x << ", " << clipSpace.y << ", " << clipSpace.z << ", " << clipSpace.w << ")" << std::endl;

    if (clipSpace.w > 0.0001f) {
        glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
        glm::vec2 screen = glm::vec2((ndc.x * 0.5f + 0.5f) * WINDOW_WIDTH, (ndc.y * 0.5f + 0.5f) * WINDOW_HEIGHT);
        std::cout << "  NDC: (" << ndc.x << ", " << ndc.y << ", " << ndc.z << ")" << std::endl;
        std::cout << "  Screen: (" << screen.x << ", " << screen.y << ")" << std::endl;
        std::cout << "  ✅ Vertex is IN FRONT of camera (w > 0)" << std::endl;
    } else {
        std::cout << "  ❌ Vertex is BEHIND camera (w <= 0)" << std::endl;
    }
    std::cout << std::endl;

    std::cout << "[INFO] Suggested positions to try:" << std::endl;
    std::cout << "       1. Close-up: " << center.x << ", " << center.y << ", " << center.z + size.z * 0.5f << std::endl;
    std::cout << "       2. Far view: " << center.x << ", " << maxBounds.y + 50.0f << ", " << maxBounds.z + size.z << std::endl;
    std::cout << "       3. Inside: " << center.x << ", " << center.y << ", " << center.z << std::endl;

    GPUBuffer gpuBuffer;
    gpuBuffer.uploadMesh(testMesh);

    std::cout << "\n[INFO] Mesh successfully loaded and uploaded to GPU" << std::endl;
    std::cout << "[INFO] GPU Vertex Buffer ID: " << gpuBuffer.getVertexBufferID() << std::endl;
    std::cout << "[INFO] GPU Face Buffer ID: " << gpuBuffer.getFaceBufferID() << std::endl;

    std::cout << "\n[GPU READBACK TEST] Verifying Face buffer upload..." << std::endl;
    std::vector<Face> gpuFaces(5);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBuffer.getFaceBufferID());
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 5 * sizeof(Face), gpuFaces.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    for (int i = 0; i < 5; i++) {
        std::cout << "  GPU Face " << i << ": indices=(" << gpuFaces[i].indices.x << ", " 
                  << gpuFaces[i].indices.y << ", " << gpuFaces[i].indices.z << ")" << std::endl;
    }

    bool facesMatch = true;
    for (int i = 0; i < 5; i++) {
        if (gpuFaces[i].indices.x != testMesh.faces[i].indices.x ||
            gpuFaces[i].indices.y != testMesh.faces[i].indices.y ||
            gpuFaces[i].indices.z != testMesh.faces[i].indices.z) {
            facesMatch = false;
            break;
        }
    }

    if (facesMatch) {
        std::cout << "[GPU READBACK] ✅ Face buffer uploaded correctly!" << std::endl;
    } else {
        std::cout << "[GPU READBACK] ❌ Face buffer CORRUPTED during upload!" << std::endl;
    }

    std::cout << "========================================\n" << std::endl;

    // ============================================
    // INITIALIZE TILE-BASED RASTERIZER
    // ============================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "INITIALIZING TILE-BASED RASTERIZER" << std::endl;
    std::cout << "========================================\n" << std::endl;

    TileRasterizer tileRasterizer(WINDOW_WIDTH, WINDOW_HEIGHT);
    tileRasterizer.initialize(testMesh.getFaceCount());

    std::cout << "========================================\n" << std::endl;

    // ============================================
    // 9. MAIN RENDER LOOP
    // ============================================
    std::cout << "[INFO] Controls: WASD = Move, Mouse = Look, Space/Shift = Up/Down, ESC = Exit" << std::endl;
    std::cout << "[INFO] Press P = Print camera, 0 = Debug mode, 1-9 = Jump to preset positions" << std::endl;
    std::cout << "[INFO] TIP: After pressing a number key, SLOWLY ROTATE MOUSE IN A FULL CIRCLE" << std::endl;
    std::cout << "[INFO] Initial camera: (" << camera.getPosition().x << ", " 
              << camera.getPosition().y << ", " << camera.getPosition().z << ")" << std::endl;
    std::cout << "[INFO] Looking forward: (" << camera.getFront().x << ", " 
              << camera.getFront().y << ", " << camera.getFront().z << ")\n" << std::endl;

    int frameCount = 0;
    double lastFPSTime = glfwGetTime();
    double lastDebugTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        frameCount++;
        if (currentFrame - lastFPSTime >= 1.0) {
            std::cout << "[FPS] " << frameCount << " fps (" 
                      << (1000.0 / frameCount) << " ms/frame)" << std::endl;

            if (currentFrame - lastDebugTime >= 5.0) {
                std::cout << "\n[AUTO-DEBUG] Press 'P' to see mesh bounds and camera position!" << std::endl;
                std::cout << "[AUTO-DEBUG] Mesh center: (" << meshCenter.x << ", " << meshCenter.y << ", " << meshCenter.z << ")" << std::endl;
                std::cout << "[AUTO-DEBUG] Mesh size: (" << meshSize.x << ", " << meshSize.y << ", " << meshSize.z << ")" << std::endl;
                std::cout << "[AUTO-DEBUG] Camera: (" << camera.getPosition().x << ", " << camera.getPosition().y << ", " << camera.getPosition().z << ")\n" << std::endl;
                lastDebugTime = currentFrame;
            }

            frameCount = 0;
            lastFPSTime = currentFrame;
        }

        processInput(window, deltaTime);
        glfwPollEvents();

        GLuint numGroupsX = (WINDOW_WIDTH + 15) / 16;
        GLuint numGroupsY = (WINDOW_HEIGHT + 15) / 16;

        float aspectRatio = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
        glm::mat4 viewProjection = camera.getViewProjectionMatrix(aspectRatio, camera.getFOV());

        // ----------------------------------------
        // PHASE 0: Clear Depth Buffer (GPU-accelerated)
        // ----------------------------------------
        clearDepthShader.use();
        glBindImageTexture(0, depthBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
        clearDepthShader.dispatch(numGroupsX, numGroupsY, 1);

        // Clear tile counters
        clearTilesShader.use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, tileRasterizer.getTileCounterBufferID());
        clearTilesShader.setUInt("numElements", static_cast<GLuint>(tileRasterizer.getTotalTiles()));
        GLuint numTileGroups = (tileRasterizer.getTotalTiles() + 255) / 256;
        clearTilesShader.dispatch(numTileGroups, 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // ----------------------------------------
        // PHASE 1: Project & Count Triangles Per Tile
        // ----------------------------------------
        pass1Shader.use();

        gpuBuffer.bindVertexBuffer(0);
        gpuBuffer.bindFaceBuffer(1);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, tileRasterizer.getProjectedTriangleBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tileRasterizer.getTileCounterBufferID());

        pass1Shader.setMat4("viewProjection", viewProjection);
        pass1Shader.setIVec2("screenSize", glm::ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
        pass1Shader.setIVec2("numTiles", glm::ivec2(tileRasterizer.getNumTilesX(), tileRasterizer.getNumTilesY()));

        GLuint numTriangleGroups = (static_cast<GLuint>(testMesh.getFaceCount()) + 255) / 256;
        pass1Shader.dispatch(numTriangleGroups, 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        static bool firstPassDebug = true;
        if (firstPassDebug) {
            firstPassDebug = false;

            GLint vpLoc = glGetUniformLocation(pass1Shader.getProgramID(), "viewProjection");
            GLint ssLoc = glGetUniformLocation(pass1Shader.getProgramID(), "screenSize");
            std::cout << "\n[DIAG] pass1 uniform locations: viewProjection=" << vpLoc
                      << ", screenSize=" << ssLoc << std::endl;
            std::cout << "[DIAG] pass1 program ID: " << pass1Shader.getProgramID() << std::endl;
            std::cout << "[DIAG] viewProjection[0][0]=" << viewProjection[0][0]
                      << " [1][1]=" << viewProjection[1][1]
                      << " [2][2]=" << viewProjection[2][2]
                      << " [3][3]=" << viewProjection[3][3] << std::endl;

            std::cout << "[DIAG] GPU Vertex readback test:" << std::endl;
            Vertex gpuVert0;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpuBuffer.getVertexBufferID());
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(Vertex), &gpuVert0);
            std::cout << "  GPU Vertex 0: (" << gpuVert0.position.x << ", "
                      << gpuVert0.position.y << ", " << gpuVert0.position.z << ")" << std::endl;
            std::cout << "  CPU Vertex 0: (" << testMesh.vertices[0].position.x << ", "
                      << testMesh.vertices[0].position.y << ", " << testMesh.vertices[0].position.z << ")" << std::endl;
            Vertex gpuVertFace0;
            size_t faceIdx0 = testMesh.faces[0].indices.x;
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, faceIdx0 * sizeof(Vertex), sizeof(Vertex), &gpuVertFace0);
            std::cout << "  GPU Vertex " << faceIdx0 << ": (" << gpuVertFace0.position.x << ", "
                      << gpuVertFace0.position.y << ", " << gpuVertFace0.position.z << ")" << std::endl;
            std::cout << "  CPU Vertex " << faceIdx0 << ": (" << testMesh.vertices[faceIdx0].position.x << ", "
                      << testMesh.vertices[faceIdx0].position.y << ", " << testMesh.vertices[faceIdx0].position.z << ")" << std::endl;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            glm::vec4 testClip = viewProjection * glm::vec4(gpuVertFace0.position, 1.0f);
            std::cout << "  CPU projection of GPU vertex " << faceIdx0 << ": w=" << testClip.w << std::endl;

            std::vector<ProjectedTriangle> firstTriangles(10);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileRasterizer.getProjectedTriangleBufferID());
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 10 * sizeof(ProjectedTriangle), firstTriangles.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            std::cout << "\n[PASS1 DEBUG] First 5 projected triangles:" << std::endl;
            for (int i = 0; i < 5; i++) {
                const auto& tri = firstTriangles[i];
                std::cout << "  Triangle " << i << ": AABB=(" << tri.aabbMin.x << "," << tri.aabbMin.y 
                          << ") to (" << tri.aabbMax.x << "," << tri.aabbMax.y << ")" << std::endl;
                std::cout << "           Screen pos: (" << tri.v0.x << "," << tri.v0.y << "), ("
                          << tri.v1.x << "," << tri.v1.y << "), (" << tri.v2.x << "," << tri.v2.y << ")" << std::endl;
                std::cout << "           Depths: " << tri.depth0 << ", " << tri.depth1 << ", " << tri.depth2 << std::endl;
            }

            std::cout << "\n[FACE DEBUG] First 5 face indices (CPU):" << std::endl;
            for (int i = 0; i < 5; i++) {
                const auto& face = testMesh.faces[i];
                std::cout << "  Face " << i << ": indices=(" << face.indices.x << ", " 
                          << face.indices.y << ", " << face.indices.z << ")" << std::endl;
                std::cout << "         Vertex positions: ";
                std::cout << "(" << testMesh.vertices[face.indices.x].position.x << "," 
                          << testMesh.vertices[face.indices.x].position.y << "," 
                          << testMesh.vertices[face.indices.x].position.z << "), ";
                std::cout << "(" << testMesh.vertices[face.indices.y].position.x << "," 
                          << testMesh.vertices[face.indices.y].position.y << "," 
                          << testMesh.vertices[face.indices.y].position.z << "), ";
                std::cout << "(" << testMesh.vertices[face.indices.z].position.x << "," 
                          << testMesh.vertices[face.indices.z].position.y << "," 
                          << testMesh.vertices[face.indices.z].position.z << ")" << std::endl;
            }
            std::cout << std::endl;
        }

        // ----------------------------------------
        // PHASE 1.5: Prefix Sum (Calculate Tile Offsets)
        // ----------------------------------------
        pass2Shader.use();

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, tileRasterizer.getTileCounterBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, tileRasterizer.getTileOffsetBufferID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, tileRasterizer.getBlockSumBufferID());

        GLuint numScanGroups = (tileRasterizer.getTotalTiles() + 255) / 256;

        pass2Shader.setUInt("numElements", static_cast<GLuint>(tileRasterizer.getTotalTiles()));
        pass2Shader.setUInt("pass", 0u);
        pass2Shader.dispatch(numScanGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        if (numScanGroups > 1) {
            pass2Shader.setUInt("numElements", static_cast<GLuint>(numScanGroups));
            pass2Shader.setUInt("pass", 1u);
            pass2Shader.dispatch(1, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        pass2Shader.setUInt("numElements", static_cast<GLuint>(tileRasterizer.getTotalTiles()));
        pass2Shader.setUInt("pass", 2u);
        pass2Shader.dispatch(numScanGroups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        size_t totalPairs = tileRasterizer.pass2_prefixSum();

        static bool firstFrameDebug = true;
        if (firstFrameDebug) {
            firstFrameDebug = false;
            std::cout << "\n[DEBUG] ========== FIRST FRAME DEBUG ==========" << std::endl;
            std::cout << "[DEBUG] Total triangle-tile pairs: " << totalPairs << std::endl;
            std::cout << "[DEBUG] Camera position: (" << camera.getPosition().x << ", " 
                      << camera.getPosition().y << ", " << camera.getPosition().z << ")" << std::endl;
            std::cout << "[DEBUG] Camera looking at: (" << camera.getFront().x << ", " 
                      << camera.getFront().y << ", " << camera.getFront().z << ")" << std::endl;
            std::cout << "[DEBUG] FOV: " << camera.getFOV() << " degrees" << std::endl;

            std::vector<GLuint> firstTileCounts(10);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, tileRasterizer.getTileCounterBufferID());
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 10 * sizeof(GLuint), firstTileCounts.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            std::cout << "[DEBUG] First 10 tile triangle counts: ";
            for (int i = 0; i < 10; i++) {
                std::cout << firstTileCounts[i] << " ";
            }
            std::cout << std::endl;

            GLuint maxCount = *std::max_element(firstTileCounts.begin(), firstTileCounts.end());
            std::cout << "[DEBUG] Max triangles in displayed tiles: " << maxCount << std::endl;

            if (totalPairs == 0) {
                std::cout << "\n[WARNING] ========================================" << std::endl;
                std::cout << "[WARNING] NO TRIANGLES VISIBLE!" << std::endl;
                std::cout << "[WARNING] All geometry is outside the view frustum." << std::endl;
                std::cout << "[WARNING] Try:" << std::endl;
                std::cout << "[WARNING]   - Press 1, 2, or 3 to jump to preset positions" << std::endl;
                std::cout << "[WARNING]   - Use WASD to move around" << std::endl;
                std::cout << "[WARNING]   - Scroll wheel to change FOV" << std::endl;
                std::cout << "[WARNING] ========================================\n" << std::endl;
            }
            std::cout << "[DEBUG] =========================================\n" << std::endl;
        }

        // ----------------------------------------
        // PHASE 2: Bin Triangles Into Tile Lists
        // ----------------------------------------
        if (totalPairs > 0) {
            tileRasterizer.pass3_binTriangles(testMesh.getFaceCount());

            pass3Shader.use();

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, tileRasterizer.getProjectedTriangleBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, tileRasterizer.getTileCounterBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, tileRasterizer.getTileOffsetBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tileRasterizer.getTriangleListBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, tileRasterizer.getTileWriteOffsetBufferID());

            pass3Shader.setIVec2("numTiles", glm::ivec2(tileRasterizer.getNumTilesX(), tileRasterizer.getNumTilesY()));
            pass3Shader.setUInt("maxTriangleListSize", static_cast<GLuint>(tileRasterizer.getTotalTriangleTilePairs()));

            pass3Shader.dispatch(numTriangleGroups, 1, 1);

            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // ----------------------------------------
        // PHASE 3: Rasterize with Tile Culling
        // ----------------------------------------
        if (debugMode) {
            debugTilesShader.use();
            glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            debugTilesShader.setIVec2("screenSize", glm::ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
            debugTilesShader.dispatch(numGroupsX, numGroupsY, 1);
        } else if (totalPairs > 0) {
            pass4Shader.use();

            glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(1, depthBuffer, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);

            gpuBuffer.bindVertexBuffer(2);
            gpuBuffer.bindFaceBuffer(3);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, tileRasterizer.getProjectedTriangleBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, tileRasterizer.getTileOffsetBufferID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, tileRasterizer.getTriangleListBufferID());

            pass4Shader.setIVec2("screenSize", glm::ivec2(WINDOW_WIDTH, WINDOW_HEIGHT));
            pass4Shader.setIVec2("numTiles", glm::ivec2(tileRasterizer.getNumTilesX(), tileRasterizer.getNumTilesY()));

            pass4Shader.dispatch(numGroupsX, numGroupsY, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, outputTexture);
            std::vector<float> debugColor(WINDOW_WIDTH * WINDOW_HEIGHT * 4, 0.0f);
            for (size_t i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; i++) {
                debugColor[i * 4 + 0] = 0.2f;
                debugColor[i * 4 + 1] = 0.0f;
                debugColor[i * 4 + 2] = 0.0f;
                debugColor[i * 4 + 3] = 1.0f;
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 
                           GL_RGBA, GL_FLOAT, debugColor.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        // ----------------------------------------
        // PHASE 4: Display Pass (Draw to Screen)
        // ----------------------------------------
        glClear(GL_COLOR_BUFFER_BIT);

        displayShader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, outputTexture);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
    }

    // ============================================
    // 10. CLEANUP
    // ============================================
    glDeleteTextures(1, &outputTexture);
    glDeleteTextures(1, &depthBuffer);
    glDeleteVertexArrays(1, &quadVAO);

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[INFO] Engine shutdown complete" << std::endl;

    return 0;
}
