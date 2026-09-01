#pragma once

#include "common/types.hpp"
#include "coordinator.grpc.pb.h"
#include "metadata.grpc.pb.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dse {

// Bully algorithm leader election for small clusters.
class CoordinatorServiceImpl final : public coordinator::CoordinatorService::Service {
public:
    CoordinatorServiceImpl(std::string node_id, std::vector<std::string> peer_addresses,
                           std::shared_ptr<class MetadataStore> metadata_store);

    void start();
    void stop();

    grpc::Status RequestElection(grpc::ServerContext* ctx,
                                 const coordinator::ElectionRequest* req,
                                 coordinator::ElectionResponse* resp) override;
    grpc::Status AnnounceLeader(grpc::ServerContext* ctx,
                                const coordinator::LeaderAnnounceRequest* req,
                                coordinator::LeaderAnnounceResponse* resp) override;
    grpc::Status CoordinatorHeartbeat(grpc::ServerContext* ctx,
                                      const coordinator::CoordinatorHeartbeatRequest* req,
                                      coordinator::CoordinatorHeartbeatResponse* resp) override;
    grpc::Status TriggerRecovery(grpc::ServerContext* ctx,
                                 const coordinator::TriggerRecoveryRequest* req,
                                 coordinator::TriggerRecoveryResponse* resp) override;
    grpc::Status GetLeader(grpc::ServerContext* ctx,
                           const coordinator::GetLeaderRequest* req,
                           coordinator::GetLeaderResponse* resp) override;

    bool is_leader() const { return is_leader_.load(); }
    std::string leader_id() const;
    uint64_t generation() const { return generation_.load(); }

private:
    void election_loop();
    void start_election();
    uint64_t node_priority() const;

    std::string node_id_;
    std::vector<std::string> peer_addresses_;
    std::shared_ptr<class MetadataStore> metadata_store_;
    std::atomic<bool> is_leader_{false};
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> running_{false};
    std::thread election_thread_;
    mutable std::mutex leader_mutex_;
    std::string current_leader_;
};

}  // namespace dse
