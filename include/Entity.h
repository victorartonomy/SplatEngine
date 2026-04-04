#ifndef ENTITY_H
#define ENTITY_H

#include <string>
#include <cstdint>
#include "AssetHandle.h"
#include "Transform.h"

struct Entity {
    uint32_t    id      = 0;
    std::string name;
    Transform   transform;
    AssetHandle meshAsset;       // handle into AssetManager, default invalid (id=0)
    bool        visible = true;
};

#endif // ENTITY_H
