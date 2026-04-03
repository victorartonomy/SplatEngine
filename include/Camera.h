#ifndef CAMERA_H
#define CAMERA_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

enum class CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

class Camera {
public:
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = -90.0f,
           float pitch = 0.0f);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio, float fov = 45.0f) const;
    glm::mat4 getViewProjectionMatrix(float aspectRatio, float fov = 45.0f) const;

    void processKeyboard(CameraMovement direction, float deltaTime);
    void processMouseMovement(float xOffset, float yOffset, bool constrainPitch = true);
    void processMouseScroll(float yOffset);
    void processOrbit(float xOffset, float yOffset, const glm::vec3& pivot);

    glm::vec3 getPosition() const { return m_position; }
    glm::vec3 getFront() const { return m_front; }
    float getFOV() const { return m_fov; }
    float getYaw() const { return m_yaw; }
    float getPitch() const { return m_pitch; }
    float getMovementSpeed() const { return m_movementSpeed; }
    float getMouseSensitivity() const { return m_mouseSensitivity; }
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }
    glm::quat getOrientation() const { return m_orientation; }
    void setOrientation(const glm::quat& q) { m_orientation = q; updateCameraVectors(); }

    void setPosition(const glm::vec3& position) { m_position = position; }
    void setMovementSpeed(float speed) { m_movementSpeed = speed; }
    void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
    void setYaw(float yaw);
    void setPitch(float pitch);
    void setFOV(float fov);
    void lookAt(const glm::vec3& target);

private:

    glm::vec3 m_position;
    glm::quat m_orientation;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;

    float m_yaw;   // for UI only
    float m_pitch; // for UI only

    float m_movementSpeed;
    float m_mouseSensitivity;
    float m_fov;

    float m_nearPlane;
    float m_farPlane;

    void updateCameraVectors();
};

#endif // CAMERA_H
