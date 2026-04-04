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

bool AssetManager::parseMesh(const std::string& filePath, MeshAsset& asset) {
    asset.filePath = filePath;

    if (!COFFParser::loadFromFile(filePath, asset.mesh) ||
        asset.mesh.getVertexCount() == 0 || asset.mesh.getFaceCount() == 0) {
        std::cerr << "[ERROR] AssetManager: failed to load mesh: " << filePath << std::endl;
        return false;
    }

    // Compute object-space bounds
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

void AssetManager::uploadToGPU(MeshAsset& asset) {
    asset.gpuBuffer.uploadMesh(asset.mesh);
}

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

AssetHandle AssetManager::load(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Dedup check
    AssetHandle existing = findByPath(filePath);
    if (existing.isValid()) {
        m_entries[existing.id]->refCount.fetch_add(1);
        return existing;
    }

    // Create new entry
    auto entry = std::make_unique<Entry>();
    if (!parseMesh(filePath, entry->asset))
        return AssetHandle{0};

    uploadToGPU(entry->asset);
    entry->cpuReady = true;
    entry->gpuReady = true;
    entry->refCount = 1;

    AssetHandle handle{m_nextId++};
    std::string path = entry->asset.filePath;
    m_entries[handle.id] = std::move(entry);
    bus().publish(AssetLoadedEvent{handle, path});
    return handle;
}

// ============================================
// ASYNC LOAD
// ============================================

AssetHandle AssetManager::loadAsync(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Dedup check
    AssetHandle existing = findByPath(filePath);
    if (existing.isValid()) {
        m_entries[existing.id]->refCount.fetch_add(1);
        return existing;
    }

    auto entry = std::make_unique<Entry>();
    entry->asset.filePath = filePath;
    entry->refCount = 1;

    // Capture raw pointer — safe because Entry lives in m_entries until destroyed
    Entry* rawPtr = entry.get();
    std::string pathCopy = filePath;
    entry->parseFuture = std::async(std::launch::async,
        [rawPtr, pathCopy]() -> bool {
            bool ok = parseMesh(pathCopy, rawPtr->asset);
            rawPtr->cpuReady = ok;
            return ok;
        });

    AssetHandle handle{m_nextId++};
    m_entries[handle.id] = std::move(entry);
    return handle;
}

// ============================================
// QUERY
// ============================================

bool AssetManager::isReady(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return false;
    return it->second->gpuReady;
}

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

void AssetManager::addRef(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return;
    it->second->refCount.fetch_add(1);
}

void AssetManager::release(AssetHandle handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return;
    int prev = it->second->refCount.fetch_sub(1);
    if (prev <= 1)
        it->second->pendingDestroy = true;
}

// ============================================
// PER-FRAME UPDATE (main thread only)
// ============================================

void AssetManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Drain pending GPU uploads
    for (auto& [id, entry] : m_entries) {
        if (entry->cpuReady && !entry->gpuReady) {
            if (entry->parseFuture.valid()) {
                auto status = entry->parseFuture.wait_for(std::chrono::seconds(0));
                if (status != std::future_status::ready)
                    continue;
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

    // 2. Destroy entries with refcount 0 and pendingDestroy
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (it->second->pendingDestroy && it->second->refCount <= 0) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================
// SHUTDOWN
// ============================================

void AssetManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Wait for any pending async loads
    for (auto& [id, entry] : m_entries) {
        if (entry->parseFuture.valid())
            entry->parseFuture.wait();
    }

    // Clear all entries — GPUBuffer destructors run on main thread (safe)
    m_entries.clear();
    m_nextId = 1;
}

// ============================================
// EDITOR INTROSPECTION
// ============================================

size_t AssetManager::getAssetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& [id, entry] : m_entries) {
        if (!entry->pendingDestroy)
            count++;
    }
    return count;
}

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

int AssetManager::getRefCount(AssetHandle handle) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(handle.id);
    if (it == m_entries.end()) return 0;
    return it->second->refCount.load();
}
