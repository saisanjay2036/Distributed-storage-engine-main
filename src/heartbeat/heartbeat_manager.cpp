#include "heartbeat/heartbeat_manager.hpp"
#include "network/grpc_util.hpp"
#include <spdlog/spdlog.h>
#include <thread>

namespace dse {

HeartbeatManager::HeartbeatManager(std::string node_id, std::string metadata_address,
                                   uint32_t interval_sec, uint32_t timeout_sec)
    : node_id_(std::move(node_id)),
      metadata_address_(std::move(metadata_address)),
      interval_sec_(interval_sec),
      timeout_sec_(timeout_sec) {
    metadata_stub_ = metadata::MetadataService::NewStub(create_channel(metadata_address_));
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::set_failure_callback(FailureCallback cb) {
    failure_callback_ = std::move(cb);
}

void HeartbeatManager::register_with_metadata(const std::string& address, uint64_t total_bytes) {
    metadata::RegisterNodeRequest req;
    req.set_node_id(node_id_);
    req.set_address(address);
    req.set_total_bytes(total_bytes);
    metadata::RegisterNodeResponse resp;
    grpc::ClientContext ctx;
    metadata_stub_->RegisterNode(&ctx, req, &resp);
    spdlog::info("Registered node {} at {}", node_id_, address);
}

void HeartbeatManager::start(uint64_t free_bytes, uint64_t chunk_count) {
    if (running_.exchange(true)) return;
    free_bytes_ = free_bytes;
    chunk_count_ = chunk_count;

    heartbeat_thread_ = std::thread([this]() { heartbeat_loop(); });
}

void HeartbeatManager::stop() {
    if (!running_.exchange(false)) return;
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

void HeartbeatManager::heartbeat_loop() {
    while (running_) {
        metadata::NodeHeartbeatRequest req;
        req.set_node_id(node_id_);
        req.set_timestamp_ms(now_ms());
        req.set_free_bytes(free_bytes_.load());
        req.set_chunk_count(chunk_count_.load());
        metadata::NodeHeartbeatResponse resp;
        grpc::ClientContext ctx;
        auto status = metadata_stub_->NodeHeartbeat(&ctx, req, &resp);
        if (!status.ok()) {
            spdlog::warn("Heartbeat failed for {}: {}", node_id_, status.error_message());
        }
        for (uint32_t i = 0; i < interval_sec_ && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

}  // namespace dse
