#ifndef ENTITY_H
#define ENTITY_H

#include <string>
#include <cstdint>
#include <optional>
#include "AssetHandle.h"
#include "Light.h"
#include "Transform.h"

struct Entity {
    uint32_t    id      = 0;
    std::string name;
    Transform   transform;
    AssetHandle meshAsset;       // handle into AssetManager, default invalid (id=0)
    bool        visible = true;
    std::optional<Light> light;  // present = this entity is a light
};

#endif // ENTITY_H
