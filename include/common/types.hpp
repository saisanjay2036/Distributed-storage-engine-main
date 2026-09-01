#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace dse {

using TimestampMs = uint64_t;

inline TimestampMs now_ms() {
    return static_cast<TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

struct NodeInfo {
    std::string id;
    std::string address;
    bool online{false};
    uint64_t free_bytes{0};
    uint64_t total_bytes{0};
    uint64_t chunk_count{0};
    TimestampMs last_heartbeat{0};
};

struct ReplicaInfo {
    std::string node_id;
    std::string address;
    bool is_primary{false};
    uint32_t version{0};
};

struct ChunkInfo {
    std::string chunk_id;
    std::string object_id;
    uint32_t chunk_index{0};
    uint64_t size{0};
    std::string checksum;
    uint32_t version{0};
    std::vector<ReplicaInfo> replicas;
};

struct ObjectInfo {
    std::string object_id;
    std::string filename;
    uint64_t size{0};
    uint32_t replication_factor{3};
    uint32_t version{0};
    std::string checksum;
    std::string owner;
    TimestampMs created_at{0};
    TimestampMs updated_at{0};
    std::vector<ChunkInfo> chunks;
};

enum class LoadBalanceStrategy {
    RoundRobin,
    LeastUsed,
    Random
};

enum class WriteAckPolicy {
    Majority,
    All
};

enum class NodeState {
    Online,
    Offline,
    Suspect,
    Recovering
};

struct ClusterConfig {
    uint32_t replication_factor{3};
    uint32_t chunk_size_mb{4};
    uint32_t heartbeat_interval_sec{5};
    uint32_t heartbeat_timeout_sec{15};
    LoadBalanceStrategy load_balance{LoadBalanceStrategy::LeastUsed};
    WriteAckPolicy write_ack{WriteAckPolicy::Majority};
    std::string metadata_address{"localhost:50051"};
    std::string coordinator_address{"localhost:50052"};
};

}  // namespace dse
