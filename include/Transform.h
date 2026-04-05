#ifndef TRANSFORM_H
#define TRANSFORM_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Transform — stores the position, orientation, and scale of a scene entity.
// Euler rotation angles are in degrees (XYZ order) for easy ImGui editing.
// Call getModelMatrix() to get the final 4x4 model matrix for GPU upload.
struct Transform {
    glm::vec3 position{0.0f};   // World-space translation
    glm::vec3 rotation{0.0f};   // Euler angles in degrees (X=pitch, Y=yaw, Z=roll)
    glm::vec3 scale{1.0f};      // Per-axis scale factors

    // Builds a model matrix: T * Rx * Ry * Rz * S
    // Applied order (right-to-left): scale first, then Z/Y/X rotations, then translate.
    glm::mat4 getModelMatrix() const;
};

#endif // TRANSFORM_H
