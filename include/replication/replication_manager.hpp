#pragma once

#include "chunk/chunk_format.hpp"
#include "common/types.hpp"
#include "storage.grpc.pb.h"
#include "metadata.grpc.pb.h"
#include <memory>
#include <string>
#include <vector>

namespace dse {

class ReplicationManager {
public:
    ReplicationManager(std::string local_node_id, std::string local_address,
                       metadata::MetadataService::Stub* metadata_stub);

    struct ReplicationResult {
        bool success{false};
        std::string message;
        std::vector<ReplicaInfo> replicas;
    };

    ReplicationResult replicate_chunk(const ChunkInfo& chunk_info,
                                      const std::vector<uint8_t>& data,
                                      uint32_t replication_factor,
                                      WriteAckPolicy ack_policy);

    bool replicate_to_node(const std::string& address, const ChunkHeaderData& header,
                           const std::vector<uint8_t>& data);

    std::optional<std::vector<uint8_t>> read_from_replica(const ReplicaInfo& replica,
                                                          const std::string& chunk_id);

private:
    std::unique_ptr<storage::StorageService::Stub> stub_for(const std::string& address);

    std::string local_node_id_;
    std::string local_address_;
    metadata::MetadataService::Stub* metadata_stub_;
};

}  // namespace dse
