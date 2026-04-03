#ifndef TRANSFORM_H
#define TRANSFORM_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};  // Euler degrees (XYZ) — for ImGui editing
    glm::vec3 scale{1.0f};

    glm::mat4 getModelMatrix() const;
};

#endif // TRANSFORM_H
