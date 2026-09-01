#include "coordinator/coordinator_service.hpp"
#include "metadata/metadata_store.hpp"
#include "network/grpc_util.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <thread>

namespace dse {

CoordinatorServiceImpl::CoordinatorServiceImpl(std::string node_id,
                                               std::vector<std::string> peer_addresses,
                                               std::shared_ptr<MetadataStore> metadata_store)
    : node_id_(std::move(node_id)),
      peer_addresses_(std::move(peer_addresses)),
      metadata_store_(std::move(metadata_store)) {}

uint64_t CoordinatorServiceImpl::node_priority() const {
    // Higher node_id lexicographically = higher priority (bully algorithm)
    uint64_t priority = 0;
    for (char c : node_id_) priority = priority * 256 + static_cast<unsigned char>(c);
    return priority;
}

void CoordinatorServiceImpl::start() {
    if (running_.exchange(true)) return;
    election_thread_ = std::thread([this]() { election_loop(); });
}

void CoordinatorServiceImpl::stop() {
    running_ = false;
    if (election_thread_.joinable()) election_thread_.join();
}

void CoordinatorServiceImpl::election_loop() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    while (running_) {
        if (!is_leader_.load()) {
            start_election();
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void CoordinatorServiceImpl::start_election() {
    uint64_t my_priority = node_priority();
    bool higher_peer_exists = false;

    for (const auto& peer : peer_addresses_) {
        auto channel = create_channel(peer);
        auto stub = coordinator::CoordinatorService::NewStub(channel);
        coordinator::ElectionRequest req;
        req.set_candidate_id(node_id_);
        req.set_priority(my_priority);
        coordinator::ElectionResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
        auto status = stub->RequestElection(&ctx, req, &resp);
        if (status.ok() && !resp.ack()) {
            higher_peer_exists = true;
            break;
        }
    }

    if (!higher_peer_exists) {
        is_leader_ = true;
        generation_++;
        {
            std::lock_guard lock(leader_mutex_);
            current_leader_ = node_id_;
        }
        metadata_store_->set_leader(node_id_, generation_.load());
        spdlog::info("Node {} elected as leader (gen={})", node_id_, generation_.load());

        for (const auto& peer : peer_addresses_) {
            auto channel = create_channel(peer);
            auto stub = coordinator::CoordinatorService::NewStub(channel);
            coordinator::LeaderAnnounceRequest req;
            req.set_leader_id(node_id_);
            req.set_generation(generation_.load());
            coordinator::LeaderAnnounceResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
            stub->AnnounceLeader(&ctx, req, &resp);
        }
    }
}

grpc::Status CoordinatorServiceImpl::RequestElection(grpc::ServerContext*,
                                                     const coordinator::ElectionRequest* req,
                                                     coordinator::ElectionResponse* resp) {
    uint64_t my_priority = node_priority();
    if (req->priority() < my_priority) {
        resp->set_ack(true);
        resp->set_current_leader(current_leader_);
        start_election();
    } else {
        resp->set_ack(false);
        resp->set_current_leader(current_leader_);
    }
    return grpc::Status::OK;
}

grpc::Status CoordinatorServiceImpl::AnnounceLeader(grpc::ServerContext*,
                                                    const coordinator::LeaderAnnounceRequest* req,
                                                    coordinator::LeaderAnnounceResponse* resp) {
    std::lock_guard lock(leader_mutex_);
    current_leader_ = req->leader_id();
    is_leader_ = (req->leader_id() == node_id_);
    generation_ = req->generation();
    metadata_store_->set_leader(req->leader_id(), req->generation());
    resp->set_ack(true);
    spdlog::info("Leader announced: {} (gen={})", req->leader_id(), req->generation());
    return grpc::Status::OK;
}

grpc::Status CoordinatorServiceImpl::CoordinatorHeartbeat(grpc::ServerContext*,
                                                          const coordinator::CoordinatorHeartbeatRequest*,
                                                          coordinator::CoordinatorHeartbeatResponse* resp) {
    resp->set_success(true);
    resp->set_leader_id(current_leader_);
    resp->set_generation(generation_.load());
    return grpc::Status::OK;
}

grpc::Status CoordinatorServiceImpl::TriggerRecovery(grpc::ServerContext*,
                                                     const coordinator::TriggerRecoveryRequest* req,
                                                     coordinator::TriggerRecoveryResponse* resp) {
    if (!is_leader_.load()) {
        resp->set_success(false);
        resp->set_message("Not leader");
        return grpc::Status::OK;
    }
    metadata_store_->mark_node_offline(req->failed_node_id());
    resp->set_success(true);
    resp->set_message("Recovery triggered");
    resp->set_chunks_recovered(0);
    return grpc::Status::OK;
}

grpc::Status CoordinatorServiceImpl::GetLeader(grpc::ServerContext*,
                                             const coordinator::GetLeaderRequest*,
                                             coordinator::GetLeaderResponse* resp) {
    resp->set_leader_id(current_leader_);
    resp->set_generation(generation_.load());
    resp->set_is_leader(is_leader_.load());
    return grpc::Status::OK;
}

std::string CoordinatorServiceImpl::leader_id() const {
    std::lock_guard lock(leader_mutex_);
    return current_leader_;
}

}  // namespace dse
