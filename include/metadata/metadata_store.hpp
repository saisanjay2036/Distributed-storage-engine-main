#pragma once

#include "common/types.hpp"
#include "common/rw_lock.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dse {

class MetadataStore {
public:
    explicit MetadataStore(const std::string& persist_path = "./metadata_store.json");

    std::optional<ObjectInfo> create_object(const std::string& filename, uint64_t size,
                                            uint32_t replication_factor, const std::string& owner,
                                            uint32_t chunk_size_bytes);
    bool delete_object(const std::string& object_id);
    bool delete_object_by_filename(const std::string& filename);
    std::optional<ObjectInfo> locate_by_id(const std::string& object_id) const;
    std::optional<ObjectInfo> locate_by_filename(const std::string& filename) const;
    std::vector<ObjectInfo> list_objects(const std::string& prefix, uint32_t limit, uint32_t offset) const;
    bool update_chunk(const std::string& object_id, const ChunkInfo& chunk);
    bool register_node(const NodeInfo& node);
    bool update_node_heartbeat(const std::string& node_id, uint64_t free_bytes, uint64_t chunk_count);
    bool mark_node_offline(const std::string& node_id);
    std::vector<NodeInfo> get_nodes() const;
    std::optional<NodeInfo> get_node(const std::string& node_id) const;
    std::vector<ChunkInfo> get_under_replicated_chunks(uint32_t replication_factor) const;
    std::vector<NodeInfo> select_nodes(uint32_t count, LoadBalanceStrategy strategy) const;
    void set_leader(const std::string& leader_id, uint64_t generation);
    std::string leader_id() const;
    uint64_t generation() const;
    bool persist();
    bool load();

private:
    std::vector<ChunkInfo> plan_chunks(const std::string& object_id, uint64_t size,
                                       uint32_t chunk_size_bytes) const;

    std::string persist_path_;
    mutable ReadWriteLock lock_;
    std::unordered_map<std::string, ObjectInfo> objects_by_id_;
    std::unordered_map<std::string, std::string> filename_to_id_;
    std::unordered_map<std::string, NodeInfo> nodes_;
    std::string leader_id_;
    uint64_t generation_{0};
    mutable size_t round_robin_index_{0};
};

}  // namespace dse
