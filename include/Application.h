#ifndef APPLICATION_H
#define APPLICATION_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

#include "Camera.h"
#include "CameraState.h"
#include "Renderer.h"
#include "Scene.h"

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool initialize();
    void run();
    void shutdown();

private:
    void update();

    // Window
    GLFWwindow* m_window = nullptr;
    int m_windowWidth  = 1600;
    int m_windowHeight = 900;

    // Renderer (owns the compute pipeline, GPU resources, merged mesh)
    Renderer m_renderer;

    // Scene
    Camera m_camera{glm::vec3(0.0f, 0.0f, 100.0f)};
    Scene  m_scene;
    int    m_selectedEntity = -1;   // index into m_scene.getEntities(), or -1

    // Editor state
    std::vector<CameraState> m_bookmarks;
    float  m_deltaTime  = 0.0f;
    float  m_lastFrame  = 0.0f;
    int    m_frameCount = 0;
    double m_lastFPSTime = 0.0;
    float  m_currentFPS  = 0.0f;
    bool   m_firstLoop   = true;
};

#endif // APPLICATION_H
