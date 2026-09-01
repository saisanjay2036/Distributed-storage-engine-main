#include "replication/replication_manager.hpp"
#include "network/grpc_util.hpp"
#include "common/config.hpp"
#include "common/checksum.hpp"
#include <spdlog/spdlog.h>

namespace dse {

ReplicationManager::ReplicationManager(std::string local_node_id, std::string local_address,
                                       metadata::MetadataService::Stub* metadata_stub)
    : local_node_id_(std::move(local_node_id)),
      local_address_(std::move(local_address)),
      metadata_stub_(metadata_stub) {}

std::unique_ptr<storage::StorageService::Stub>
ReplicationManager::stub_for(const std::string& address) {
    return storage::StorageService::NewStub(create_channel(address));
}

bool ReplicationManager::replicate_to_node(const std::string& address,
                                             const ChunkHeaderData& header,
                                             const std::vector<uint8_t>& data) {
    auto stub = stub_for(address);
    storage::ReplicateChunkRequest req;
    auto* h = req.mutable_header();
    h->set_chunk_id(header.chunk_id);
    h->set_object_id(header.object_id);
    h->set_version(header.version);
    h->set_size(header.size);
    h->set_checksum(header.checksum);
    h->set_created_at_ms(header.created_at_ms);
    req.set_data(data.data(), data.size());
    req.set_source_node_id(local_node_id_);

    storage::ReplicateChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(30));
    auto status = stub->ReplicateChunk(&ctx, req, &resp);
    return status.ok() && resp.success();
}

ReplicationManager::ReplicationResult
ReplicationManager::replicate_chunk(const ChunkInfo& chunk_info,
                                    const std::vector<uint8_t>& data,
                                    uint32_t replication_factor,
                                    WriteAckPolicy ack_policy) {
    ReplicationResult result;

    metadata::SelectNodesRequest sel_req;
    sel_req.set_count(replication_factor);
    sel_req.set_strategy("least_used");
    metadata::SelectNodesResponse sel_resp;
    grpc::ClientContext ctx;
    if (!metadata_stub_->SelectNodes(&ctx, sel_req, &sel_resp).ok() ||
        sel_resp.nodes_size() == 0) {
        result.message = "No available nodes";
        return result;
    }

    ChunkHeaderData header;
    header.chunk_id = chunk_info.chunk_id;
    header.object_id = chunk_info.object_id;
    header.version = chunk_info.version;
    header.size = data.size();
    header.checksum = Checksum::sha256(data);
    header.created_at_ms = now_ms();

    uint32_t success_count = 0;
    bool primary_set = false;

    for (int i = 0; i < sel_resp.nodes_size(); ++i) {
        const auto& node = sel_resp.nodes(i);
        bool ok = false;

        if (node.node_id() == local_node_id_) {
            auto stub = stub_for(local_address_);
            storage::WriteChunkRequest write_req;
            auto* h = write_req.mutable_header();
            h->set_chunk_id(header.chunk_id);
            h->set_object_id(header.object_id);
            h->set_version(header.version);
            h->set_size(header.size);
            h->set_checksum(header.checksum);
            write_req.set_data(data.data(), data.size());
            storage::WriteChunkResponse write_resp;
            grpc::ClientContext wctx;
            ok = stub->WriteChunk(&wctx, write_req, &write_resp).ok() && write_resp.success();
        } else {
            ok = replicate_to_node(node.address(), header, data);
        }

        if (ok) {
            ++success_count;
            ReplicaInfo rep;
            rep.node_id = node.node_id();
            rep.address = node.address();
            rep.is_primary = !primary_set;
            rep.version = chunk_info.version;
            primary_set = true;
            result.replicas.push_back(rep);
        }
    }

    uint32_t required = (ack_policy == WriteAckPolicy::All)
                            ? replication_factor
                            : (replication_factor / 2 + 1);

    result.success = success_count >= required;
    result.message = result.success
                         ? "Replication successful"
                         : "Insufficient replicas (" + std::to_string(success_count) + "/" +
                               std::to_string(required) + ")";
    return result;
}

std::optional<std::vector<uint8_t>>
ReplicationManager::read_from_replica(const ReplicaInfo& replica, const std::string& chunk_id) {
    auto stub = stub_for(replica.address);
    storage::ReadChunkRequest req;
    req.set_chunk_id(chunk_id);
    storage::ReadChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(30));
    auto status = stub->ReadChunk(&ctx, req, &resp);
    if (!status.ok() || !resp.success()) return std::nullopt;
    return std::vector<uint8_t>(resp.data().begin(), resp.data().end());
}

}  // namespace dse
