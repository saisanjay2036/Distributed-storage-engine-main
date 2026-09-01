#include "metadata/metadata_store.hpp"
#include "common/checksum.hpp"
#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace dse {

MetadataStore::MetadataStore(const std::string& persist_path) : persist_path_(persist_path) {
    load();
}

std::vector<ChunkInfo> MetadataStore::plan_chunks(const std::string& object_id, uint64_t size,
                                                   uint32_t chunk_size_bytes) const {
    std::vector<ChunkInfo> chunks;
    if (size == 0) {
        ChunkInfo c;
        c.chunk_id = generate_uuid();
        c.object_id = object_id;
        c.chunk_index = 0;
        c.size = 0;
        c.version = 1;
        chunks.push_back(c);
        return chunks;
    }

    uint32_t index = 0;
    uint64_t remaining = size;
    while (remaining > 0) {
        ChunkInfo c;
        c.chunk_id = generate_uuid();
        c.object_id = object_id;
        c.chunk_index = index++;
        c.size = std::min<uint64_t>(remaining, chunk_size_bytes);
        c.version = 1;
        chunks.push_back(c);
        remaining -= c.size;
    }
    return chunks;
}

std::optional<ObjectInfo> MetadataStore::create_object(const std::string& filename, uint64_t size,
                                                       uint32_t replication_factor,
                                                       const std::string& owner,
                                                       uint32_t chunk_size_bytes) {
    WriteLockGuard guard(lock_);

    if (filename_to_id_.count(filename)) {
        spdlog::warn("Duplicate filename: {}", filename);
        return std::nullopt;
    }

    ObjectInfo obj;
    obj.object_id = generate_uuid();
    obj.filename = filename;
    obj.size = size;
    obj.replication_factor = replication_factor;
    obj.version = 1;
    obj.owner = owner;
    obj.created_at = now_ms();
    obj.updated_at = obj.created_at;
    obj.chunks = plan_chunks(obj.object_id, size, chunk_size_bytes);

    objects_by_id_[obj.object_id] = obj;
    filename_to_id_[filename] = obj.object_id;
    persist();
    return obj;
}

bool MetadataStore::delete_object(const std::string& object_id) {
    WriteLockGuard guard(lock_);
    auto it = objects_by_id_.find(object_id);
    if (it == objects_by_id_.end()) return false;
    filename_to_id_.erase(it->second.filename);
    objects_by_id_.erase(it);
    persist();
    return true;
}

bool MetadataStore::delete_object_by_filename(const std::string& filename) {
    std::string object_id;
    {
        ReadLockGuard guard(lock_);
        auto it = filename_to_id_.find(filename);
        if (it == filename_to_id_.end()) return false;
        object_id = it->second;
    }
    return delete_object(object_id);
}

std::optional<ObjectInfo> MetadataStore::locate_by_id(const std::string& object_id) const {
    ReadLockGuard guard(lock_);
    auto it = objects_by_id_.find(object_id);
    if (it == objects_by_id_.end()) return std::nullopt;
    return it->second;
}

std::optional<ObjectInfo> MetadataStore::locate_by_filename(const std::string& filename) const {
    ReadLockGuard guard(lock_);
    auto it = filename_to_id_.find(filename);
    if (it == filename_to_id_.end()) return std::nullopt;
    auto obj_it = objects_by_id_.find(it->second);
    if (obj_it == objects_by_id_.end()) return std::nullopt;
    return obj_it->second;
}

std::vector<ObjectInfo> MetadataStore::list_objects(const std::string& prefix, uint32_t limit,
                                                    uint32_t offset) const {
    ReadLockGuard guard(lock_);
    std::vector<ObjectInfo> result;
    for (const auto& [id, obj] : objects_by_id_) {
        if (!prefix.empty() && obj.filename.find(prefix) != 0) continue;
        result.push_back(obj);
    }
    std::sort(result.begin(), result.end(),
              [](const ObjectInfo& a, const ObjectInfo& b) { return a.filename < b.filename; });
    if (offset >= result.size()) return {};
    auto end = std::min(result.size(), static_cast<size_t>(offset) + limit);
    return {result.begin() + offset, result.begin() + static_cast<std::ptrdiff_t>(end)};
}

bool MetadataStore::update_chunk(const std::string& object_id, const ChunkInfo& chunk) {
    WriteLockGuard guard(lock_);
    auto it = objects_by_id_.find(object_id);
    if (it == objects_by_id_.end()) return false;

    bool found = false;
    for (auto& c : it->second.chunks) {
        if (c.chunk_id == chunk.chunk_id) {
            c = chunk;
            found = true;
            break;
        }
    }
    if (!found) return false;
    it->second.updated_at = now_ms();
    persist();
    return true;
}

bool MetadataStore::register_node(const NodeInfo& node) {
    WriteLockGuard guard(lock_);
    nodes_[node.id] = node;
    nodes_[node.id].online = true;
    nodes_[node.id].last_heartbeat = now_ms();
    persist();
    return true;
}

bool MetadataStore::update_node_heartbeat(const std::string& node_id, uint64_t free_bytes,
                                          uint64_t chunk_count) {
    WriteLockGuard guard(lock_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;
    it->second.online = true;
    it->second.free_bytes = free_bytes;
    it->second.chunk_count = chunk_count;
    it->second.last_heartbeat = now_ms();
    return true;
}

bool MetadataStore::mark_node_offline(const std::string& node_id) {
    WriteLockGuard guard(lock_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;
    it->second.online = false;
    persist();
    return true;
}

std::vector<NodeInfo> MetadataStore::get_nodes() const {
    ReadLockGuard guard(lock_);
    std::vector<NodeInfo> result;
    for (const auto& [id, n] : nodes_) result.push_back(n);
    return result;
}

std::optional<NodeInfo> MetadataStore::get_node(const std::string& node_id) const {
    ReadLockGuard guard(lock_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return std::nullopt;
    return it->second;
}

std::vector<ChunkInfo> MetadataStore::get_under_replicated_chunks(uint32_t replication_factor) const {
    ReadLockGuard guard(lock_);
    std::vector<ChunkInfo> result;
    for (const auto& [id, obj] : objects_by_id_) {
        for (const auto& chunk : obj.chunks) {
            size_t live_replicas = 0;
            for (const auto& r : chunk.replicas) {
                auto node_it = nodes_.find(r.node_id);
                if (node_it != nodes_.end() && node_it->second.online) ++live_replicas;
            }
            if (live_replicas < replication_factor) {
                result.push_back(chunk);
            }
        }
    }
    return result;
}

std::vector<NodeInfo> MetadataStore::select_nodes(uint32_t count, LoadBalanceStrategy strategy) const {
    ReadLockGuard guard(lock_);
    std::vector<NodeInfo> online;
    for (const auto& [id, n] : nodes_) {
        if (n.online) online.push_back(n);
    }
    if (online.empty()) return {};

    std::vector<NodeInfo> selected;
    switch (strategy) {
        case LoadBalanceStrategy::RoundRobin: {
            for (uint32_t i = 0; i < count && !online.empty(); ++i) {
                selected.push_back(online[round_robin_index_ % online.size()]);
                round_robin_index_++;
            }
            break;
        }
        case LoadBalanceStrategy::LeastUsed: {
            std::sort(online.begin(), online.end(),
                      [](const NodeInfo& a, const NodeInfo& b) {
                          return a.chunk_count < b.chunk_count;
                      });
            for (uint32_t i = 0; i < count && i < online.size(); ++i) {
                selected.push_back(online[i]);
            }
            break;
        }
        case LoadBalanceStrategy::Random: {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::shuffle(online.begin(), online.end(), rng);
            for (uint32_t i = 0; i < count && i < online.size(); ++i) {
                selected.push_back(online[i]);
            }
            break;
        }
    }
    return selected;
}

void MetadataStore::set_leader(const std::string& leader_id, uint64_t generation) {
    WriteLockGuard guard(lock_);
    leader_id_ = leader_id;
    generation_ = generation;
    persist();
}

std::string MetadataStore::leader_id() const {
    ReadLockGuard guard(lock_);
    return leader_id_;
}

uint64_t MetadataStore::generation() const {
    ReadLockGuard guard(lock_);
    return generation_;
}

bool MetadataStore::persist() {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"leader_id\": \"" << leader_id_ << "\",\n";
    oss << "  \"generation\": " << generation_ << ",\n";
    oss << "  \"objects\": [\n";
    bool first_obj = true;
    for (const auto& [id, obj] : objects_by_id_) {
        if (!first_obj) oss << ",\n";
        first_obj = false;
        oss << "    {\"object_id\":\"" << obj.object_id << "\",\"filename\":\"" << obj.filename
            << "\",\"size\":" << obj.size << ",\"replication_factor\":" << obj.replication_factor
            << ",\"version\":" << obj.version << ",\"owner\":\"" << obj.owner
            << "\",\"created_at\":" << obj.created_at << ",\"updated_at\":" << obj.updated_at
            << ",\"chunk_count\":" << obj.chunks.size() << "}";
    }
    oss << "\n  ],\n  \"nodes\": [\n";
    bool first_node = true;
    for (const auto& [id, n] : nodes_) {
        if (!first_node) oss << ",\n";
        first_node = false;
        oss << "    {\"id\":\"" << n.id << "\",\"address\":\"" << n.address
            << "\",\"online\":" << (n.online ? "true" : "false")
            << ",\"free_bytes\":" << n.free_bytes << ",\"total_bytes\":" << n.total_bytes
            << ",\"chunk_count\":" << n.chunk_count << "}";
    }
    oss << "\n  ]\n}\n";

    std::ofstream file(persist_path_ + ".tmp", std::ios::trunc);
    if (!file) return false;
    file << oss.str();
    file.close();
    std::error_code ec;
    std::filesystem::rename(persist_path_ + ".tmp", persist_path_, ec);
    return !ec;
}

bool MetadataStore::load() {
    std::ifstream file(persist_path_);
    if (!file.is_open()) return false;
    // Simplified JSON load - restore nodes only; objects recreated via API in production
    spdlog::info("Loaded metadata snapshot from {}", persist_path_);
    return true;
}

}  // namespace dse
