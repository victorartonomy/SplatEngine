#include "InputManager.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

void InputManager::initialize(GLFWwindow* window) {
    m_window = window;

    m_actionBindings[Action::CameraFwd]   = GLFW_KEY_W;
    m_actionBindings[Action::CameraBack]  = GLFW_KEY_S;
    m_actionBindings[Action::CameraLeft]  = GLFW_KEY_A;
    m_actionBindings[Action::CameraRight] = GLFW_KEY_D;
    m_actionBindings[Action::CameraUp]    = GLFW_KEY_SPACE;
    m_actionBindings[Action::CameraDown]  = GLFW_KEY_LEFT_SHIFT;
    m_actionBindings[Action::Quit]        = GLFW_KEY_ESCAPE;
}

void InputManager::update(const ImGuiIO& io, bool viewportHovered) {
    for (size_t i = 0; i < static_cast<size_t>(Action::Count); ++i) {
        Action action = static_cast<Action>(i);
        auto it = m_actionBindings.find(action);
        if (it != m_actionBindings.end())
            m_actionState[i] = (glfwGetKey(m_window, it->second) == GLFW_PRESS);
        else
            m_actionState[i] = false;
    }

    m_rmbDown = io.MouseDown[1];
    m_lmbDown = io.MouseDown[0];
    m_mmbDown = io.MouseDown[2];
    m_altDown = io.KeyAlt;

    m_axisValues[static_cast<size_t>(Axis::MouseX)]     = io.MouseDelta.x;
    m_axisValues[static_cast<size_t>(Axis::MouseY)]     = io.MouseDelta.y;
    m_axisValues[static_cast<size_t>(Axis::MouseScrollY)] = io.MouseWheel;

    m_viewportHovered = viewportHovered;
}

bool InputManager::isActionDown(Action action) const {
    return m_actionState[static_cast<size_t>(action)];
}

float InputManager::getAxis(Axis axis) const {
    return m_axisValues[static_cast<size_t>(axis)];
}

bool InputManager::isViewportHovered() const { return m_viewportHovered; }
bool InputManager::isRMBDown()         const { return m_rmbDown; }
bool InputManager::isLMBDown()         const { return m_lmbDown; }
bool InputManager::isMMBDown()         const { return m_mmbDown; }
bool InputManager::isAltDown()         const { return m_altDown; }

void InputManager::bindAction(Action action, int glfwKey) {
    m_actionBindings[action] = glfwKey;
}

int InputManager::getBoundKey(Action action) const {
    auto it = m_actionBindings.find(action);
    return (it != m_actionBindings.end()) ? it->second : GLFW_KEY_UNKNOWN;
}
