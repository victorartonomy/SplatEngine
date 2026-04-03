#ifndef ENTITY_H
#define ENTITY_H

#include <string>
#include <cstdint>
#include "Transform.h"

struct Entity {
    uint32_t    id             = 0;
    std::string name;
    Transform   transform;
    int         meshAssetIndex = -1;  // index into Scene::m_meshAssets, -1 = none
    bool        visible        = true;
};

#endif // ENTITY_H
