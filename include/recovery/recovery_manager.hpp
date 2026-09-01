#pragma once

#include "common/types.hpp"
#include "metadata.grpc.pb.h"
#include "replication/replication_manager.hpp"
#include <atomic>
#include <memory>
#include <thread>

namespace dse {

class RecoveryManager {
public:
    RecoveryManager(metadata::MetadataService::Stub* metadata_stub,
                    ReplicationManager* replication_manager);

    struct RecoveryResult {
        bool success{false};
        uint32_t chunks_recovered{0};
        std::string message;
    };

    RecoveryResult recover_failed_node(const std::string& failed_node_id);
    RecoveryResult repair_under_replicated_chunks();
    void start_background_recovery(uint32_t interval_sec);
    void stop();

private:
    void recovery_loop();
    std::optional<std::vector<uint8_t>> fetch_chunk_from_replica(const ChunkInfo& chunk,
                                                                 const std::string& exclude_node);

    metadata::MetadataService::Stub* metadata_stub_;
    ReplicationManager* replication_manager_;
    std::atomic<bool> running_{false};
    std::thread recovery_thread_;
    uint32_t interval_sec_{30};
};

}  // namespace dse
