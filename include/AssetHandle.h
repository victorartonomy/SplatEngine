#ifndef ASSET_HANDLE_H
#define ASSET_HANDLE_H

#include <cstdint>

struct AssetHandle {
    uint32_t id = 0;
    bool isValid() const { return id != 0; }
    bool operator==(const AssetHandle& o) const { return id == o.id; }
    bool operator!=(const AssetHandle& o) const { return id != o.id; }
};

#endif // ASSET_HANDLE_H
