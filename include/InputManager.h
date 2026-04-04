#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <GLFW/glfw3.h>

#include <array>
#include <unordered_map>

struct ImGuiIO;

enum class Action {
    CameraFwd, CameraBack, CameraLeft, CameraRight, CameraUp, CameraDown,
    Quit,
    Count
};

enum class Axis {
    MouseX, MouseY, MouseScrollY,
    Count
};

class InputManager {
public:
    void initialize(GLFWwindow* window);

    // Call once per frame AFTER ImGui::NewFrame()
    void update(const ImGuiIO& io, bool viewportHovered);

    bool  isActionDown(Action action) const;
    float getAxis(Axis axis) const;

    bool isViewportHovered() const;
    bool isRMBDown() const;
    bool isLMBDown() const;
    bool isMMBDown() const;
    bool isAltDown() const;

    void bindAction(Action action, int glfwKey);
    int  getBoundKey(Action action) const;

private:
    GLFWwindow* m_window = nullptr;

    std::unordered_map<Action, int> m_actionBindings;

    std::array<bool,  static_cast<size_t>(Action::Count)> m_actionState{};
    std::array<float, static_cast<size_t>(Axis::Count)>   m_axisValues{};

    bool m_viewportHovered = false;
    bool m_rmbDown = false;
    bool m_lmbDown = false;
    bool m_mmbDown = false;
    bool m_altDown = false;
};

#endif // INPUT_MANAGER_H
