#include "Camera.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : m_position(position)
    , m_worldUp(up)
    , m_orientation(glm::quat(glm::vec3(glm::radians(-pitch), glm::radians(-yaw), 0.0f)))
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
    // Robust up vector: if direction is parallel to up, use fallback
    glm::vec3 up = m_worldUp;
    if (glm::abs(glm::dot(direction, up)) > 0.999f) {
        up = glm::vec3(0, 0, 1);
        if (glm::abs(glm::dot(direction, up)) > 0.999f)
            up = glm::vec3(1, 0, 0);
    }
    m_orientation = glm::quatLookAt(direction, up);
    updateCameraVectors();
}

void Camera::processMouseMovement(float xOffset, float yOffset, bool /*constrainPitch*/) {
    xOffset *= m_mouseSensitivity;
    yOffset *= m_mouseSensitivity;

    // Yaw (Y axis), Pitch (X axis)
    glm::quat qPitch = glm::angleAxis(glm::radians(-yOffset), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-xOffset), glm::vec3(0, 1, 0));
    m_orientation = qYaw * m_orientation;
    m_orientation = m_orientation * qPitch;
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

void Camera::processMouseScroll(float yOffset) {
    // Scroll wheel adjusts movement speed (multiplicative for natural feel)
    float factor = (yOffset > 0.0f) ? 1.1f : 1.0f / 1.1f;
    m_movementSpeed *= factor;
    m_movementSpeed = std::clamp(m_movementSpeed, 1.0f, 10000.0f);
}

void Camera::processOrbit(float xOffset, float yOffset, const glm::vec3& pivot) {
    xOffset *= m_mouseSensitivity;
    yOffset *= m_mouseSensitivity;

    // Calculate vector from pivot to camera
    glm::vec3 toCam = m_position - pivot;
    float radius = glm::length(toCam);
    if (radius < 1e-4f) radius = 1.0f;
    glm::quat qPitch = glm::angleAxis(glm::radians(-yOffset), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-xOffset), glm::vec3(0, 1, 0));
    glm::quat rot = qYaw * qPitch;
    toCam = rot * toCam;
    m_position = pivot + toCam;
    // Look at the pivot (robust up)
    glm::vec3 direction = glm::normalize(pivot - m_position);
    glm::vec3 up = m_worldUp;
    if (glm::abs(glm::dot(direction, up)) > 0.999f) {
        up = glm::vec3(0, 0, 1);
        if (glm::abs(glm::dot(direction, up)) > 0.999f)
            up = glm::vec3(1, 0, 0);
    }
    m_orientation = glm::quatLookAt(direction, up);
    updateCameraVectors();
}

void Camera::setYaw(float yaw) {
    m_yaw = yaw;
    // Rebuild orientation from yaw/pitch
    glm::quat qPitch = glm::angleAxis(glm::radians(-m_pitch), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-m_yaw), glm::vec3(0, 1, 0));
    m_orientation = qYaw * qPitch;
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

void Camera::setPitch(float pitch) {
    m_pitch = pitch;
    // Rebuild orientation from yaw/pitch
    glm::quat qPitch = glm::angleAxis(glm::radians(-m_pitch), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-m_yaw), glm::vec3(0, 1, 0));
    m_orientation = qYaw * qPitch;
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

void Camera::setFOV(float fov) {
    m_fov = std::clamp(fov, 1.0f, 90.0f);
}

void Camera::updateCameraVectors() {
    m_front = m_orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    m_right = m_orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    m_up    = m_orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    // Keep yaw/pitch in sync with quaternion for UI/bookmarks
    glm::vec3 euler = glm::eulerAngles(m_orientation);
    m_pitch = -glm::degrees(euler.x);
    m_yaw   = -glm::degrees(euler.y);
}
