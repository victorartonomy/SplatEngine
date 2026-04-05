#include "InputManager.h"
#include "EventBus.h"
#include "Events.h"

#include <imgui.h>
#include <GLFW/glfw3.h>

// Initialize the InputManager with a GLFW window and set up default key bindings.
// All action-to-key mappings are defined here. To rebind a key at runtime, call
// bindAction(action, newGlfwKey) after initialization.
void InputManager::initialize(GLFWwindow* window) {
    m_window = window;

    // Default camera movement bindings — WASD + Space/Shift for 6DOF flight
    m_actionBindings[Action::CameraFwd]   = GLFW_KEY_W;
    m_actionBindings[Action::CameraBack]  = GLFW_KEY_S;
    m_actionBindings[Action::CameraLeft]  = GLFW_KEY_A;
    m_actionBindings[Action::CameraRight] = GLFW_KEY_D;
    m_actionBindings[Action::CameraUp]    = GLFW_KEY_SPACE;
    m_actionBindings[Action::CameraDown]  = GLFW_KEY_LEFT_SHIFT;
    m_actionBindings[Action::Quit]        = GLFW_KEY_ESCAPE;
}

// Poll all registered action bindings and mouse/axis state from ImGui IO.
// Must be called once per frame before any system queries action or axis values.
//
// Rising/falling edge detection:
//   m_prevActionState is saved before the new state is sampled. Any action whose
//   state differs from last frame triggers an ActionEvent published to the event bus.
//   This allows one-shot handlers (e.g., Quit on key-press) without polling isActionDown()
//   every frame — subscribers react exactly once on the transition.
//
// viewportHovered controls whether camera input should be processed; camera movement
// code checks isViewportHovered() to avoid moving the camera while hovering ImGui panels.
void InputManager::update(const ImGuiIO& io, bool viewportHovered) {
    // Save last frame's state for edge detection before overwriting it
    m_prevActionState = m_actionState;

    // Poll each registered action from GLFW and detect state transitions
    for (size_t i = 0; i < static_cast<size_t>(Action::Count); ++i) {
        Action action = static_cast<Action>(i);
        auto it = m_actionBindings.find(action);
        if (it != m_actionBindings.end())
            // glfwGetKey returns GLFW_PRESS or GLFW_RELEASE; map to bool
            m_actionState[i] = (glfwGetKey(m_window, it->second) == GLFW_PRESS);
        else
            m_actionState[i] = false;  // No binding → treat as unpressed

        // Publish ActionEvent on rising edge (key down) or falling edge (key up)
        if (m_actionState[i] != m_prevActionState[i])
            bus().publish(ActionEvent{action, m_actionState[i]});
    }

    // Cache mouse button states from ImGui IO (avoids GLFW queries for mouse)
    m_rmbDown = io.MouseDown[1];  // Right mouse button
    m_lmbDown = io.MouseDown[0];  // Left mouse button
    m_mmbDown = io.MouseDown[2];  // Middle mouse button
    m_altDown = io.KeyAlt;        // Alt key (used for orbit mode activation)

    // Cache mouse delta and scroll wheel as float axes.
    // MouseDelta is the pixel displacement since last frame; MouseWheel is the
    // scroll amount in "ticks" (can be fractional on high-precision trackpads).
    m_axisValues[static_cast<size_t>(Axis::MouseX)]      = io.MouseDelta.x;
    m_axisValues[static_cast<size_t>(Axis::MouseY)]      = io.MouseDelta.y;
    m_axisValues[static_cast<size_t>(Axis::MouseScrollY)] = io.MouseWheel;

    m_viewportHovered = viewportHovered;
}

// Returns true while the action's key is held down (continuous input).
// Use this for camera movement. For one-shot responses (e.g., toggling UI),
// subscribe to ActionEvent on the event bus instead.
bool InputManager::isActionDown(Action action) const {
    return m_actionState[static_cast<size_t>(action)];
}

// Returns the current float value for an axis this frame.
// Mouse deltas are in pixels; scroll wheel is in ticks.
float InputManager::getAxis(Axis axis) const {
    return m_axisValues[static_cast<size_t>(axis)];
}

// Query functions for mouse button and modifier key states.
// These are read from ImGui IO rather than GLFW directly so ImGui can
// intercept and block input when a panel has focus (io.WantCaptureMouse).
bool InputManager::isViewportHovered() const { return m_viewportHovered; }
bool InputManager::isRMBDown()         const { return m_rmbDown; }
bool InputManager::isLMBDown()         const { return m_lmbDown; }
bool InputManager::isMMBDown()         const { return m_mmbDown; }
bool InputManager::isAltDown()         const { return m_altDown; }

// Rebind an action to a new GLFW key code at runtime.
// Takes effect the next time update() is called. Replaces any existing binding.
void InputManager::bindAction(Action action, int glfwKey) {
    m_actionBindings[action] = glfwKey;
}

// Returns the GLFW key code currently bound to an action.
// Returns GLFW_KEY_UNKNOWN if no binding exists (e.g., action was never registered).
int InputManager::getBoundKey(Action action) const {
    auto it = m_actionBindings.find(action);
    return (it != m_actionBindings.end()) ? it->second : GLFW_KEY_UNKNOWN;
}
