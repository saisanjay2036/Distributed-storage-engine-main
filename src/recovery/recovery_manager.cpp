#include "recovery/recovery_manager.hpp"
#include "network/grpc_util.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>
#include <thread>

namespace dse {

RecoveryManager::RecoveryManager(metadata::MetadataService::Stub* metadata_stub,
                                 ReplicationManager* replication_manager)
    : metadata_stub_(metadata_stub), replication_manager_(replication_manager) {}

void RecoveryManager::start_background_recovery(uint32_t interval_sec) {
    interval_sec_ = interval_sec;
    if (running_.exchange(true)) return;
    recovery_thread_ = std::thread([this]() { recovery_loop(); });
}

void RecoveryManager::stop() {
    running_ = false;
    if (recovery_thread_.joinable()) recovery_thread_.join();
}

void RecoveryManager::recovery_loop() {
    while (running_) {
        auto result = repair_under_replicated_chunks();
        if (result.chunks_recovered > 0) {
            spdlog::info("Background recovery: {} chunks repaired", result.chunks_recovered);
        }
        for (uint32_t i = 0; i < interval_sec_ && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

std::optional<std::vector<uint8_t>>
RecoveryManager::fetch_chunk_from_replica(const ChunkInfo& chunk, const std::string& exclude_node) {
    for (const auto& rep : chunk.replicas) {
        if (rep.node_id == exclude_node) continue;
        auto data = replication_manager_->read_from_replica(rep, chunk.chunk_id);
        if (data) return data;
    }
    return std::nullopt;
}

RecoveryManager::RecoveryResult RecoveryManager::recover_failed_node(const std::string& failed_node_id) {
    RecoveryResult result;
    metadata::MarkNodeOfflineRequest off_req;
    off_req.set_node_id(failed_node_id);
    metadata::MarkNodeOfflineResponse off_resp;
    grpc::ClientContext ctx;
    metadata_stub_->MarkNodeOffline(&ctx, off_req, &off_resp);

    metadata::GetUnderReplicatedChunksRequest ur_req;
    metadata::GetUnderReplicatedChunksResponse ur_resp;
    grpc::ClientContext ctx2;
    metadata_stub_->GetUnderReplicatedChunks(&ctx2, ur_req, &ur_resp);

    auto& cfg = Config::instance().cluster();
    for (const auto& pb_chunk : ur_resp.chunks()) {
        ChunkInfo chunk;
        chunk.chunk_id = pb_chunk.chunk_id();
        chunk.object_id = pb_chunk.object_id();
        chunk.chunk_index = pb_chunk.chunk_index();
        chunk.size = pb_chunk.size();
        chunk.checksum = pb_chunk.checksum();
        chunk.version = pb_chunk.version();
        for (const auto& r : pb_chunk.replicas()) {
            ReplicaInfo rep;
            rep.node_id = r.node_id();
            rep.address = r.address();
            rep.is_primary = r.is_primary();
            rep.version = r.version();
            chunk.replicas.push_back(rep);
        }

        auto data = fetch_chunk_from_replica(chunk, failed_node_id);
        if (!data) continue;

        auto rep_result = replication_manager_->replicate_chunk(
            chunk, *data, cfg.replication_factor, cfg.write_ack);
        if (rep_result.success) {
            metadata::UpdateChunkRequest upd_req;
            upd_req.set_object_id(chunk.object_id);
            auto* pb = upd_req.mutable_chunk();
            pb->set_chunk_id(chunk.chunk_id);
            pb->set_object_id(chunk.object_id);
            pb->set_chunk_index(chunk.chunk_index);
            pb->set_size(chunk.size);
            pb->set_checksum(chunk.checksum);
            pb->set_version(chunk.version + 1);
            for (const auto& r : rep_result.replicas) {
                auto* rep_pb = pb->add_replicas();
                rep_pb->set_node_id(r.node_id);
                rep_pb->set_address(r.address);
                rep_pb->set_is_primary(r.is_primary);
                rep_pb->set_version(r.version);
            }
            metadata::UpdateChunkResponse upd_resp;
            grpc::ClientContext ctx3;
            metadata_stub_->UpdateChunk(&ctx3, upd_req, &upd_resp);
            ++result.chunks_recovered;
        }
    }

    result.success = true;
    result.message = "Recovery complete";
    return result;
}

RecoveryManager::RecoveryResult RecoveryManager::repair_under_replicated_chunks() {
    RecoveryResult result;
    metadata::GetUnderReplicatedChunksRequest ur_req;
    metadata::GetUnderReplicatedChunksResponse ur_resp;
    grpc::ClientContext ctx;
    if (!metadata_stub_->GetUnderReplicatedChunks(&ctx, ur_req, &ur_resp).ok()) {
        result.message = "Failed to query under-replicated chunks";
        return result;
    }

    auto& cfg = Config::instance().cluster();
    for (const auto& pb_chunk : ur_resp.chunks()) {
        ChunkInfo chunk;
        chunk.chunk_id = pb_chunk.chunk_id();
        chunk.object_id = pb_chunk.object_id();
        chunk.chunk_index = pb_chunk.chunk_index();
        chunk.size = pb_chunk.size();
        chunk.checksum = pb_chunk.checksum();
        chunk.version = pb_chunk.version();
        for (const auto& r : pb_chunk.replicas()) {
            ReplicaInfo rep;
            rep.node_id = r.node_id();
            rep.address = r.address();
            rep.is_primary = r.is_primary();
            rep.version = r.version();
            chunk.replicas.push_back(rep);
        }

        auto data = fetch_chunk_from_replica(chunk, "");
        if (!data) continue;

        auto rep_result = replication_manager_->replicate_chunk(
            chunk, *data, cfg.replication_factor, cfg.write_ack);
        if (rep_result.success) {
            metadata::UpdateChunkRequest upd_req;
            upd_req.set_object_id(chunk.object_id);
            auto* pb = upd_req.mutable_chunk();
            pb->set_chunk_id(chunk.chunk_id);
            pb->set_object_id(chunk.object_id);
            pb->set_chunk_index(chunk.chunk_index);
            pb->set_size(chunk.size);
            pb->set_checksum(chunk.checksum);
            pb->set_version(chunk.version + 1);
            for (const auto& r : rep_result.replicas) {
                auto* rep_pb = pb->add_replicas();
                rep_pb->set_node_id(r.node_id);
                rep_pb->set_address(r.address);
                rep_pb->set_is_primary(r.is_primary);
                rep_pb->set_version(r.version);
            }
            metadata::UpdateChunkResponse upd_resp;
            grpc::ClientContext upd_ctx;
            metadata_stub_->UpdateChunk(&upd_ctx, upd_req, &upd_resp);
            ++result.chunks_recovered;
        }
    }

    result.success = true;
    result.message = "Repair complete";
    return result;
}

}  // namespace dse
