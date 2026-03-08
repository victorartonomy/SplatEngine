#ifndef CAMERA_STATE_H
#define CAMERA_STATE_H

#include <glm/glm.hpp>
#include <string>

struct CameraState {
    std::string name;
    glm::vec3 position;
    float yaw;
    float pitch;
    float fov;
    float movementSpeed;
    float mouseSensitivity;
};

#endif // CAMERA_STATE_H
