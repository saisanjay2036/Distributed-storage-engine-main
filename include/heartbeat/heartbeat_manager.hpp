#pragma once

#include "common/types.hpp"
#include "metadata.grpc.pb.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dse {

class HeartbeatManager {
public:
    using FailureCallback = std::function<void(const std::string& node_id)>;

    HeartbeatManager(std::string node_id, std::string metadata_address,
                     uint32_t interval_sec, uint32_t timeout_sec);
    ~HeartbeatManager();

    void start(uint64_t free_bytes, uint64_t chunk_count);
    void stop();
    void set_failure_callback(FailureCallback cb);
    void register_with_metadata(const std::string& address, uint64_t total_bytes);

private:
    void heartbeat_loop();
    void monitor_loop();

    std::string node_id_;
    std::string metadata_address_;
    uint32_t interval_sec_;
    uint32_t timeout_sec_;
    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::thread monitor_thread_;
    std::unique_ptr<metadata::MetadataService::Stub> metadata_stub_;
    FailureCallback failure_callback_;
    std::atomic<uint64_t> free_bytes_{0};
    std::atomic<uint64_t> chunk_count_{0};
};

}  // namespace dse
