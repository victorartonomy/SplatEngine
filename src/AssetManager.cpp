#include "AssetManager.h"
#include "COFFParser.h"
#include "EventBus.h"
#include "Events.h"

#include <iostream>
#include <chrono>
#include <filesystem>

// ============================================
// STATIC HELPERS
// ============================================

// Load and parse a mesh file into `asset`. Computes object-space bounds from vertex positions.
// Called both on the main thread (synchronous load) and from a background thread (async load).
// Returns false if the file can't be read or contains no valid geometry.
bool AssetManager::parseMesh(const std::string& filePath, MeshAsset& asset) {
    asset.filePath = filePath;

    if (!COFFParser::loadFromFile(filePath, asset.mesh) ||
        asset.mesh.getVertexCount() == 0 || asset.mesh.getFaceCount() == 0) {
        std::cerr << "[ERROR] AssetManager: failed to load mesh: " << filePath << std::endl;
        return false;
    }

    // Compute tight object-space AABB by scanning all vertex positions.
    // boundsMin/Max are used by Scene::recalculateSceneBounds() to compute world-space bounds.
    asset.boundsMin = glm::vec3(1e10f);
    asset.boundsMax = glm::vec3(-1e10f);
    for (const auto& v : asset.mesh.vertices) {
        asset.boundsMin = glm::min(asset.boundsMin, v.position);
        asset.boundsMax = glm::max(asset.boundsMax, v.position);
    }
    asset.boundsCenter = (asset.boundsMin + asset.boundsMax) * 0.5f;
    asset.boundsSize   = asset.boundsMax - asset.boundsMin;

    std::cout << "[INFO] AssetManager: loaded mesh \"" << filePath << "\" ("
              << asset.mesh.getVertexCount() << " verts, "
              << asset.mesh.getFaceCount()   << " faces)" << std::endl;

    return true;
}

// Upload parsed mesh geometry to GPU SSBOs via GPUBuffer.
// Must be called on the main (OpenGL context) thread.
// Called once per asset, after parseMesh() succeeds.
void AssetManager::uploadToGPU(MeshAsset& asset) {
    asset.gpuBuffer.uploadMesh(asset.mesh);
}

// Linear scan for an already-loaded asset by file path.
// Returns the existing handle if found (for deduplication), or an invalid handle (id=0).
AssetHandle AssetManager::findByPath(const std::string& filePath) const {
    for (const auto& [id, entry] : m_entries) {
        if (!entry->pendingDestroy && entry->asset.filePath == filePath)
            return AssetHandle{id};
    }
    return AssetHandle{0};
}

// ============================================
// SYNCHRONOUS LOAD
// ============================================

// Load a mesh synchronously on the calling thread (always the main GL thread).
// If the same path is already loaded, increments its refcount and returns the existing handle.
// On success: parses, uploads to GPU, publishes AssetLoadedEvent, and returns a valid handle.
// On failure: returns an invalid handle (id=0).
//
// The path is captured before the entry is move-inserted into m_entries because after
// std::move(entry), entry->asset.filePath is no longer accessible.
AssetHandle AssetManager::load(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Deduplication: reuse existing asset and increment refcount
    AssetHandle existing = findByPath(filePath);
    if (existing.isValid()) {
        m_entries[existing.id]->refCount.fetch_add(1);
        return existing;
    }

    auto entry = std::make_unique<Entry>();
    if (!parseMesh(filePath, entry->asset))
        return AssetHandle{0};

    uploadToGPU(entry->asset);
    entry->cpuReady = true;
    entry->gpuReady = true;
    entry->refCount = 1;

    AssetHandle handle{m_nextId++};
    std::string path = entry->asset.filePath; // Capture before move
    m_entries[handle.id] = std::move(entry);
    bus().publish(AssetLoadedEvent{handle, path});
    return handle;
}

// ============================================
// ASYNC LOAD
// ============================================

// Begin loading a mesh asynchronously on a background thread.
// Returns a handle immediately; the asset is not ready until update() polls the future.
//
// Thread safety: The background task captures a raw pointer (rawPtr) to the heap-allocated
// Entry. This is safe because:
//   1. The Entry's unique_ptr is inserted into m_entries before the future starts.
//   2. Entries are only destroyed in update() after pendingDestroy is set — which cannot
//      happen before the future completes (the future result transitions cpuReady → gpuReady).
//   3. parseMesh() is static and does not access any shared AssetManager state.
//
// The handle is valid immediately but isReady() returns false until gpuReady is set.
AssetHandle AssetManager::loadAsync(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Deduplication: same path already loading or loaded
    AssetHandle existing = findByPath(filePath);
    if (existing.isValid()) {
        m_entries[existing.id]->refCount.fetch_add(1);
        return existing;
    }

    auto entry = std::make_unique<Entry>();
    entry->asset.filePath = filePath;
    entry->refCount = 1;

    // Capture raw pointer — safe as explained above
    Entry* rawPtr = entry.get();
    std::string pathCopy = filePath;
    entry->parseFuture = std::async(std::launch::async,
        [rawPtr, pathCopy]() -> bool {
            bool ok = parseMesh(pathCopy, rawPtr->asset);
            rawPtr->cpuReady = ok; // Signal that CPU-side parse is complete
            return ok;
        });

    AssetHandle handle{m_nextId++};
    m_entries[handle.id] = std::move(entry);
    return handle;
}

// ============================================
// QUERY
// ============================================

// Returns true only after GPU upload is complete (gpuReady == true).
// Use this to check before rendering an entity that was loaded asynchronously.
bool AssetManager::isReady(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return false;
    return it->second->gpuReady;
}

// Returns a const pointer to the MeshAsset for a given handle, or nullptr if not ready.
// The pointer is valid until the next call to update() on the main thread.
// Do not store this pointer across frames — an asset may be freed if its refcount drops to zero.
const MeshAsset* AssetManager::get(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return nullptr;
    if (!it->second->gpuReady) return nullptr;
    return &it->second->asset;
}

// ============================================
// REFERENCE COUNTING
// ============================================

// Increment the reference count for an asset (e.g., when cloning an entity).
// Prevents the asset from being destroyed while multiple entities reference it.
void AssetManager::addRef(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return;
    it->second->refCount.fetch_add(1);
}

// Decrement the reference count. When it reaches zero, marks the entry for deferred
// destruction. The actual deletion happens in update() after the GPU is done with the data.
// Using atomic fetch_sub: if prev <= 1, the count was 1 before decrement (now 0 or below).
void AssetManager::release(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return;
    int prev = it->second->refCount.fetch_sub(1);
    if (prev <= 1)
        it->second->pendingDestroy = true; // Will be erased in the next update() call
}

// ============================================
// PER-FRAME UPDATE (main thread only)
// ============================================

// Perform two maintenance tasks per frame:
//
// 1. GPU upload drain:
//    For each entry that has finished CPU parsing (cpuReady) but not yet been uploaded
//    to the GPU (gpuReady), poll the future with a zero-timeout to check if the background
//    thread has finished. If ready, upload to GPU and publish AssetLoadedEvent.
//    This ensures GL calls happen only on the main thread.
//
// 2. Deferred destruction:
//    Erase entries that are both pendingDestroy (refcount reached zero) and fully destroyed
//    (refCount <= 0). GPUBuffer destructor runs here on the main thread, which is correct
//    since GL objects must be freed on the GL thread.
void AssetManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Drain pending GPU uploads from async loads
    for (auto& [id, entry] : m_entries) {
        if (entry->cpuReady && !entry->gpuReady) {
            if (entry->parseFuture.valid()) {
                // Non-blocking check: only upload if the background thread finished
                auto status = entry->parseFuture.wait_for(std::chrono::seconds(0));
                if (status != std::future_status::ready)
                    continue; // Still parsing — check again next frame
                bool ok = entry->parseFuture.get();
                if (!ok) {
                    entry->pendingDestroy = true;
                    continue;
                }
            }
            uploadToGPU(entry->asset);
            entry->gpuReady = true;
            bus().publish(AssetLoadedEvent{AssetHandle{id}, entry->asset.filePath});
        }
    }

    // 2. Destroy entries whose reference count has reached zero
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (it->second->pendingDestroy && it->second->refCount <= 0) {
            it = m_entries.erase(it); // GPUBuffer freed here on main thread
        } else {
            ++it;
        }
    }
}

// ============================================
// SHUTDOWN
// ============================================

// Block until all in-flight async loads complete, then free all entries.
// Must be called before the OpenGL context is destroyed (GPUBuffer cleanup needs a valid context).
// After shutdown(), m_nextId is reset to 1 so handles from a previous session are no longer valid.
void AssetManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Wait for any pending async loads to finish before clearing
    for (auto& [id, entry] : m_entries) {
        if (entry->parseFuture.valid())
            entry->parseFuture.wait(); // Block until background thread completes
    }

    // GPUBuffer destructors run here — must be on the main GL thread
    m_entries.clear();
    m_nextId = 1;
}

// ============================================
// EDITOR INTROSPECTION
// ============================================

// Returns the count of assets that are not pending destruction.
// Used by the Asset Manager panel to show how many meshes are loaded.
size_t AssetManager::getAssetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& [id, entry] : m_entries) {
        if (!entry->pendingDestroy)
            count++;
    }
    return count;
}

// Returns a snapshot of asset metadata for display in the Asset Manager UI.
// Excludes entries that are pending destruction.
// AssetInfo includes the handle, file path, vertex/face counts, and ready state.
std::vector<AssetInfo> AssetManager::getAssetInfos() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AssetInfo> infos;
    infos.reserve(m_entries.size());
    for (const auto& [id, entry] : m_entries) {
        if (entry->pendingDestroy) continue;
        AssetInfo info;
        info.handle      = AssetHandle{id};
        info.filePath    = entry->asset.filePath;
        info.vertexCount = entry->asset.mesh.getVertexCount();
        info.faceCount   = entry->asset.mesh.getFaceCount();
        info.ready       = entry->gpuReady;
        infos.push_back(info);
    }
    return infos;
}

// Returns the current reference count for a given handle.
// A count of 0 means the asset is not referenced by any entity and may be freed soon.
int AssetManager::getRefCount(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return 0;
    return it->second->refCount.load();
}
