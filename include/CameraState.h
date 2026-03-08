#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

struct CameraState {
    std::string name;
    glm::vec3 position;
    glm::quat orientation;
    float yaw;
    float pitch;
    float fov;
    float movementSpeed;
    float mouseSensitivity;
};

#endif // CAMERA_STATE_H
