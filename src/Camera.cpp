#include "Camera.h"
#include <algorithm>
#include <glm/gtx/quaternion.hpp>

// Constructor — initializes the camera at a given position with world-up and initial yaw/pitch.
//
// The quaternion orientation is built from the initial yaw/pitch Euler angles so that
// processMouseMovement() can immediately operate on a well-defined orientation.
// Convention: negative pitch rotates down (looking at -Y), negative yaw rotates left.
//
// m_front, m_right, and m_up are extracted immediately by updateCameraVectors().
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

// Returns a view matrix that transforms world space into camera (view) space.
// glm::lookAt builds a right-handed look-at matrix from the camera position,
// a target point (position + front direction), and the camera's up vector.
glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

// Returns a perspective projection matrix for the given aspect ratio and FOV.
// m_nearPlane / m_farPlane define the view frustum depth range.
// fov is passed in degrees and converted to radians for GLM.
glm::mat4 Camera::getProjectionMatrix(float aspectRatio, float fov) const {
    return glm::perspective(glm::radians(fov), aspectRatio, m_nearPlane, m_farPlane);
}

// Returns the combined view-projection matrix (P * V).
// Used by the compute shaders to project world-space vertices to clip space.
glm::mat4 Camera::getViewProjectionMatrix(float aspectRatio, float fov) const {
    return getProjectionMatrix(aspectRatio, fov) * getViewMatrix();
}

// Move the camera in the specified direction by movementSpeed * deltaTime.
// Directions are relative to the camera's current orientation:
//   FORWARD/BACKWARD move along m_front (the look direction)
//   LEFT/RIGHT move along m_right (perpendicular to front, in the horizontal plane)
//   UP/DOWN move along m_worldUp (world +Y axis, so "up" is always vertical)
// deltaTime scaling makes movement frame-rate independent.
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

// Reorient the camera to look at a given world-space target point.
//
// Gimbal-lock avoidance: if the computed direction vector is nearly parallel to the
// world-up vector (dot product > 0.999), a fallback up vector is chosen (+Z, then +X).
// This prevents glm::quatLookAt from producing a degenerate quaternion.
//
// After setting the orientation, updateCameraVectors() re-extracts m_front/m_right/m_up
// and syncs the scalar yaw/pitch for the UI.
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

// Apply mouse movement as a quaternion rotation (FPS-style look).
//
// Mouse delta is split into two separate rotation quaternions:
//   qPitch — rotation around world X axis (vertical look, applied in local space)
//   qYaw   — rotation around world Y axis (horizontal look, applied in world space)
//
// Application order matters:
//   m_orientation = qYaw * m_orientation;         → yaw in world space (no roll accumulation)
//   m_orientation = m_orientation * qPitch;       → pitch in local space (tilts relative to current look)
//
// This order prevents the "rolling horizon" artifact that occurs when pitch is applied
// in world space while yaw is applied in local space (or both in the same space).
// The orientation is renormalized each frame to prevent floating-point drift.
void Camera::processMouseMovement(float xOffset, float yOffset, bool /*constrainPitch*/) {
    xOffset *= m_mouseSensitivity;
    yOffset *= m_mouseSensitivity;

    // Yaw (Y axis rotation), Pitch (X axis rotation); negative offsets match screen-to-world convention
    glm::quat qPitch = glm::angleAxis(glm::radians(-yOffset), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-xOffset), glm::vec3(0, 1, 0));
    m_orientation = qYaw * m_orientation;   // World-space yaw first
    m_orientation = m_orientation * qPitch; // Then local-space pitch
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

// Adjust camera movement speed via scroll wheel.
// Multiplicative scaling (×1.1 per tick up, ÷1.1 per tick down) gives a logarithmic
// feel: small speeds change slowly, large speeds change quickly.
// Clamped to [1, 10000] to prevent degenerate near-zero or excessive values.
void Camera::processMouseScroll(float yOffset) {
    float factor = (yOffset > 0.0f) ? 1.1f : 1.0f / 1.1f;
    m_movementSpeed *= factor;
    m_movementSpeed = std::clamp(m_movementSpeed, 1.0f, 10000.0f);
}

// Orbit the camera around a pivot point (e.g., scene center) using mouse delta.
//
// Algorithm:
//   1. Compute the vector from pivot to camera (toCam) and its length (radius).
//   2. Build yaw and pitch quaternions from the mouse deltas (same convention as FPS look).
//   3. Rotate toCam by the combined quaternion, keeping the radius constant.
//   4. Set the new camera position to pivot + rotated toCam.
//   5. Use lookAt() to make the camera face the pivot (with gimbal-lock fallback).
//
// This preserves orbit radius and always keeps the pivot in view.
void Camera::processOrbit(float xOffset, float yOffset, const glm::vec3& pivot) {
    xOffset *= m_mouseSensitivity;
    yOffset *= m_mouseSensitivity;

    // Vector from pivot to camera — we rotate this vector, then re-derive position
    glm::vec3 toCam = m_position - pivot;
    float radius = glm::length(toCam);
    if (radius < 1e-4f) radius = 1.0f; // Prevent degenerate zero-radius orbit

    glm::quat qPitch = glm::angleAxis(glm::radians(-yOffset), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-xOffset), glm::vec3(0, 1, 0));
    glm::quat rot = qYaw * qPitch;
    toCam = rot * toCam;               // Rotate the offset vector
    m_position = pivot + toCam;        // New camera position, same distance from pivot

    // Re-orient to face the pivot (with gimbal-lock-safe up fallback)
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

// Set the yaw angle (degrees) and rebuild the quaternion orientation from scratch.
// Used by the Camera Settings UI so users can type in exact angle values.
// Rebuilding from yaw+pitch avoids accumulating floating-point error from incremental rotations.
void Camera::setYaw(float yaw) {
    m_yaw = yaw;
    glm::quat qPitch = glm::angleAxis(glm::radians(-m_pitch), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-m_yaw),   glm::vec3(0, 1, 0));
    m_orientation = qYaw * qPitch;
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

// Set the pitch angle (degrees) and rebuild the quaternion orientation from scratch.
// Same rationale as setYaw() — clean rebuild rather than incremental delta.
void Camera::setPitch(float pitch) {
    m_pitch = pitch;
    glm::quat qPitch = glm::angleAxis(glm::radians(-m_pitch), glm::vec3(1, 0, 0));
    glm::quat qYaw   = glm::angleAxis(glm::radians(-m_yaw),   glm::vec3(0, 1, 0));
    m_orientation = qYaw * qPitch;
    m_orientation = glm::normalize(m_orientation);
    updateCameraVectors();
}

// Clamp FOV to [1°, 90°] to prevent degenerate projection (near-zero or fisheye extremes).
void Camera::setFOV(float fov) {
    m_fov = std::clamp(fov, 1.0f, 90.0f);
}

// Recompute m_front, m_right, m_up from the current quaternion orientation.
//
// The quaternion is applied to canonical basis vectors:
//   (0,0,-1) → m_front  (camera looks down -Z in view space)
//   (1,0, 0) → m_right
//   (0,1, 0) → m_up
//
// Euler angles (yaw/pitch) are extracted from the quaternion and kept in sync for the
// Camera Settings UI, which reads/writes scalar yaw/pitch. The sign conventions
// (-degrees) match the sign convention used in processMouseMovement() and setYaw/setPitch.
void Camera::updateCameraVectors() {
    m_front = m_orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    m_right = m_orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    m_up    = m_orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Keep yaw/pitch floats in sync with quaternion so the UI displays correct values
    glm::vec3 euler = glm::eulerAngles(m_orientation);
    m_pitch = -glm::degrees(euler.x);  // Negate to match our sign convention
    m_yaw   = -glm::degrees(euler.y);
}
