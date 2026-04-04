#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <future>
#include <atomic>

#include <glm/glm.hpp>

#include "AssetHandle.h"
#include "Mesh.h"
#include "GPUBuffer.h"

// MeshAsset — formerly in Scene.h, now owned by AssetManager
struct MeshAsset {
    std::string filePath;
    Mesh        mesh;
    GPUBuffer   gpuBuffer;
    glm::vec3   boundsMin{0.0f};
    glm::vec3   boundsMax{0.0f};
    glm::vec3   boundsCenter{0.0f};
    glm::vec3   boundsSize{0.0f};
};

// Lightweight info for UI display
struct AssetInfo {
    AssetHandle handle;
    std::string filePath;
    size_t      vertexCount = 0;
    size_t      faceCount   = 0;
    bool        ready       = false;
};

class AssetManager {
public:
    AssetManager()  = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Synchronous load: parse + GPU upload on calling thread.
    // Returns valid handle on success, invalid on failure.
    // On cache hit: increments refcount and returns existing handle.
    AssetHandle load(const std::string& filePath);

    // Async load: CPU parse on worker thread, GPU upload deferred to update().
    // Returns handle immediately. get() returns nullptr until ready.
    AssetHandle loadAsync(const std::string& filePath);

    // Is the asset fully loaded (CPU + GPU)?
    bool isReady(AssetHandle handle) const;

    // Get the asset data. Returns nullptr if handle is invalid or not ready.
    const MeshAsset* get(AssetHandle handle) const;

    // Reference counting
    void addRef(AssetHandle handle);
    void release(AssetHandle handle);

    // Call once per frame on main thread:
    // finalizes pending GPU uploads, destroys refcount-0 entries.
    void update();

    // Release all assets. Must be called before GL context is destroyed.
    void shutdown();

    // Editor introspection
    size_t                  getAssetCount() const;
    std::vector<AssetInfo>  getAssetInfos() const;
    int                     getRefCount(AssetHandle handle) const;

private:
    struct Entry {
        MeshAsset            asset;
        std::atomic<int>     refCount{0};
        std::atomic<bool>    cpuReady{false};
        bool                 gpuReady       = false;
        bool                 pendingDestroy = false;
        std::future<bool>    parseFuture;
    };

    // Parse mesh data and compute bounds (thread-safe, no GL calls)
    static bool parseMesh(const std::string& filePath, MeshAsset& asset);

    // Upload mesh to GPU (main thread only)
    static void uploadToGPU(MeshAsset& asset);

    // Find existing entry by file path (caller must hold m_mutex)
    AssetHandle findByPath(const std::string& filePath) const;

    mutable std::mutex                                   m_mutex;
    std::unordered_map<uint32_t, std::unique_ptr<Entry>> m_entries;
    uint32_t                                             m_nextId = 1;
};

#endif // ASSET_MANAGER_H
