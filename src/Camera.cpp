#include "Camera.h"
#include <algorithm>

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position)
    , m_worldUp(up)
    , m_yaw(yaw)
    , m_pitch(pitch)
    , m_front(glm::vec3(0.0f, 0.0f, -1.0f))
    , m_movementSpeed(50.0f)
    , m_mouseSensitivity(0.1f)
    , m_fov(45.0f)
    , m_nearPlane(0.1f)
    , m_farPlane(10000.0f)
{
    updateCameraVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio, float fov) const {
    return glm::perspective(glm::radians(fov), aspectRatio, m_nearPlane, m_farPlane);
}

glm::mat4 Camera::getViewProjectionMatrix(float aspectRatio, float fov) const {
    return getProjectionMatrix(aspectRatio, fov) * getViewMatrix();
}

void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
    float velocity = m_movementSpeed * deltaTime;

    switch (direction) {
        case CameraMovement::FORWARD:
            m_position += m_front * velocity;
            break;
        case CameraMovement::BACKWARD:
            m_position -= m_front * velocity;
            break;
        case CameraMovement::LEFT:
            m_position -= m_right * velocity;
            break;
        case CameraMovement::RIGHT:
            m_position += m_right * velocity;
            break;
        case CameraMovement::UP:
            m_position += m_worldUp * velocity;
            break;
        case CameraMovement::DOWN:
            m_position -= m_worldUp * velocity;
            break;
    }
}

void Camera::lookAt(const glm::vec3& target) {
    glm::vec3 direction = glm::normalize(target - m_position);

    m_pitch = glm::degrees(asin(direction.y));
    m_yaw = glm::degrees(atan2(direction.z, direction.x));

    updateCameraVectors();
}

void Camera::processMouseMovement(float xOffset, float yOffset, bool constrainPitch) {
    xOffset *= m_mouseSensitivity;
    yOffset *= m_mouseSensitivity;

    m_yaw += xOffset;
    m_pitch += yOffset;

    if (constrainPitch) {
        m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
    }

    updateCameraVectors();
}

void Camera::processMouseScroll(float yOffset) {
    m_fov -= yOffset;
    m_fov = std::clamp(m_fov, 1.0f, 90.0f);
}

void Camera::setYaw(float yaw) {
    m_yaw = yaw;
    updateCameraVectors();
}

void Camera::setPitch(float pitch) {
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    updateCameraVectors();
}

void Camera::setFOV(float fov) {
    m_fov = std::clamp(fov, 1.0f, 90.0f);
}

void Camera::updateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    
    m_front = glm::normalize(front);
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}
