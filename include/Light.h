#ifndef LIGHT_H
#define LIGHT_H

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#include "Transform.h"

// LightType — discriminates which attenuation and direction model to use in the shader.
//   Directional: infinite-distance, no attenuation, direction from entity rotation
//   Point:       omnidirectional falloff by distance/range
//   Spot:        cone-shaped point light with inner (full) and outer (fade) angles
enum class LightType : int { Directional = 0, Point = 1, Spot = 2 };

// ShadingModel — selects the shading algorithm in pass4_rasterize_tiled.comp.
//   BlinnPhong: diffuse + specular with a fixed shininess exponent
//   PBR:        Cook-Torrance with GGX distribution, Smith geometry, Schlick Fresnel
enum class ShadingModel : int { BlinnPhong = 0, PBR = 1 };

// Light — CPU-side per-entity light component (stored in Entity::light).
// Converted to GPULight before upload via toGPULight().
struct Light {
    LightType type      = LightType::Directional;
    glm::vec3 color{1.0f};      // Linear RGB light color
    float     intensity = 1.0f; // Scalar multiplier on color; can exceed 1 for HDR
    float     range     = 100.0f; // Maximum reach for point/spot (world units); ignored for directional
    float     innerCone = 25.0f;  // Spot: half-angle (degrees) of the full-intensity cone
    float     outerCone = 35.0f;  // Spot: half-angle (degrees) where intensity reaches zero
};

// GPULight — std430-compatible struct uploaded to SSBO binding 7.
// Must be exactly 64 bytes (4 × 16-byte vec4-aligned slots).
// Each vec3 is followed by a float to satisfy std430's 16-byte vec3 alignment rule.
struct GPULight {
    glm::vec3 position;       // bytes  0-11: world-space position (used by point/spot)
    float     type;           // bytes 12-15: LightType cast to float (avoids int alignment edge cases)
    glm::vec3 direction;      // bytes 16-27: unit vector pointing away from the light source
    float     intensity;      // bytes 28-31
    glm::vec3 color;          // bytes 32-43: linear RGB
    float     range;          // bytes 44-47: falloff distance for point/spot
    float     innerCosAngle;  // bytes 48-51: cos(innerCone) — pre-computed to avoid trig in shader
    float     outerCosAngle;  // bytes 52-55: cos(outerCone) — ditto
    float     _pad0;          // bytes 56-59: explicit padding to reach 64 bytes
    float     _pad1;          // bytes 60-63
};
static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes for std430");

// toGPULight — converts a CPU Light + Transform into a GPU-ready GPULight.
// Called by LightManager::update() for every light entity in the scene.
inline GPULight toGPULight(const Light& light, const Transform& transform) {
    GPULight g{};
    g.position  = transform.position;
    // Store type as float to avoid potential GLSL int/float packing surprises
    g.type      = static_cast<float>(static_cast<int>(light.type));
    g.intensity = light.intensity;
    g.color     = light.color;
    g.range     = light.range;

    // Build a rotation matrix from the entity's Euler angles (XYZ order)
    // then extract the -Y axis as the forward/down direction of the light.
    // -Y was chosen so that a zero-rotation directional light points straight down.
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
    rot = glm::rotate(rot, glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));
    g.direction = glm::normalize(glm::vec3(rot * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));

    // Pre-compute cosines of cone angles so the shader can use dot products directly
    // instead of calling acos(). cos(innerCone) > cos(outerCone) since smaller angle → larger cosine.
    g.innerCosAngle = std::cos(glm::radians(light.innerCone));
    g.outerCosAngle = std::cos(glm::radians(light.outerCone));
    g._pad0 = 0.0f;
    g._pad1 = 0.0f;
    return g;
}

#endif // LIGHT_H
