#ifndef LIGHT_H
#define LIGHT_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#include "Transform.h"

enum class LightType : int { Directional = 0, Point = 1, Spot = 2 };
enum class ShadingModel : int { BlinnPhong = 0, PBR = 1 };

struct Light {
    LightType type      = LightType::Directional;
    glm::vec3 color{1.0f};
    float     intensity = 1.0f;
    float     range     = 100.0f;
    float     innerCone = 25.0f;  // degrees
    float     outerCone = 35.0f;  // degrees
};

// std430-compatible GPU struct — must be 64 bytes
struct GPULight {
    glm::vec3 position;       // 12
    float     type;           // 4
    glm::vec3 direction;      // 12
    float     intensity;      // 4
    glm::vec3 color;          // 12
    float     range;          // 4
    float     innerCosAngle;  // 4
    float     outerCosAngle;  // 4
    float     _pad0;          // 4
    float     _pad1;          // 4
};
static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes for std430");

inline GPULight toGPULight(const Light& light, const Transform& transform) {
    GPULight g{};
    g.position  = transform.position;
    g.type      = static_cast<float>(static_cast<int>(light.type));
    g.intensity = light.intensity;
    g.color     = light.color;
    g.range     = light.range;

    // Direction from rotation (negative Z forward)
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
    rot = glm::rotate(rot, glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));
    g.direction = glm::normalize(glm::vec3(rot * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

    g.innerCosAngle = std::cos(glm::radians(light.innerCone));
    g.outerCosAngle = std::cos(glm::radians(light.outerCone));
    g._pad0 = 0.0f;
    g._pad1 = 0.0f;
    return g;
}

#endif // LIGHT_H
