#include "metadata/metadata_service.hpp"
#include "common/config.hpp"
#include <spdlog/spdlog.h>

namespace dse {

MetadataServiceImpl::MetadataServiceImpl(std::shared_ptr<MetadataStore> store)
    : store_(std::move(store)) {}

metadata::ObjectMetadata MetadataServiceImpl::to_proto(const ObjectInfo& obj) {
    metadata::ObjectMetadata pb;
    pb.set_object_id(obj.object_id);
    pb.set_filename(obj.filename);
    pb.set_size(obj.size);
    pb.set_replication_factor(obj.replication_factor);
    pb.set_version(obj.version);
    pb.set_checksum(obj.checksum);
    pb.set_owner(obj.owner);
    pb.set_created_at_ms(obj.created_at);
    pb.set_updated_at_ms(obj.updated_at);
    for (const auto& c : obj.chunks) {
        *pb.add_chunks() = to_proto(c);
    }
    return pb;
}

metadata::ChunkMetadata MetadataServiceImpl::to_proto(const ChunkInfo& chunk) {
    metadata::ChunkMetadata pb;
    pb.set_chunk_id(chunk.chunk_id);
    pb.set_object_id(chunk.object_id);
    pb.set_chunk_index(chunk.chunk_index);
    pb.set_size(chunk.size);
    pb.set_checksum(chunk.checksum);
    pb.set_version(chunk.version);
    for (const auto& r : chunk.replicas) {
        auto* rep = pb.add_replicas();
        rep->set_node_id(r.node_id);
        rep->set_address(r.address);
        rep->set_is_primary(r.is_primary);
        rep->set_version(r.version);
    }
    return pb;
}

ChunkInfo MetadataServiceImpl::from_proto(const metadata::ChunkMetadata& chunk) {
    ChunkInfo c;
    c.chunk_id = chunk.chunk_id();
    c.object_id = chunk.object_id();
    c.chunk_index = chunk.chunk_index();
    c.size = chunk.size();
    c.checksum = chunk.checksum();
    c.version = chunk.version();
    for (const auto& r : chunk.replicas()) {
        ReplicaInfo rep;
        rep.node_id = r.node_id();
        rep.address = r.address();
        rep.is_primary = r.is_primary();
        rep.version = r.version();
        c.replicas.push_back(rep);
    }
    return c;
}

grpc::Status MetadataServiceImpl::CreateObject(grpc::ServerContext*,
                                               const metadata::CreateObjectRequest* req,
                                               metadata::CreateObjectResponse* resp) {
    uint32_t chunk_size_mb = req->chunk_size_mb() > 0 ? req->chunk_size_mb()
                                                      : Config::instance().cluster().chunk_size_mb;
    auto obj = store_->create_object(req->filename(), req->size(),
                                     req->replication_factor() > 0 ? req->replication_factor()
                                                                   : Config::instance().cluster().replication_factor,
                                     req->owner(), chunk_size_mb * 1024U * 1024U);
    if (!obj) {
        resp->set_success(false);
        resp->set_message("Object already exists or creation failed");
        return grpc::Status::OK;
    }
    resp->set_success(true);
    *resp->mutable_object() = to_proto(*obj);
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::DeleteObject(grpc::ServerContext*,
                                               const metadata::DeleteObjectRequest* req,
                                               metadata::DeleteObjectResponse* resp) {
    bool ok = false;
    if (!req->object_id().empty()) ok = store_->delete_object(req->object_id());
    else if (!req->filename().empty()) ok = store_->delete_object_by_filename(req->filename());
    resp->set_success(ok);
    resp->set_message(ok ? "Deleted" : "Not found");
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::LocateObject(grpc::ServerContext*,
                                               const metadata::LocateObjectRequest* req,
                                               metadata::LocateObjectResponse* resp) {
    std::optional<ObjectInfo> obj;
    if (!req->object_id().empty()) obj = store_->locate_by_id(req->object_id());
    else if (!req->filename().empty()) obj = store_->locate_by_filename(req->filename());
    if (!obj) {
        resp->set_success(false);
        resp->set_message("Object not found");
        return grpc::Status::OK;
    }
    resp->set_success(true);
    *resp->mutable_object() = to_proto(*obj);
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::ListObjects(grpc::ServerContext*,
                                              const metadata::ListObjectsRequest* req,
                                              metadata::ListObjectsResponse* resp) {
    auto objects = store_->list_objects(req->prefix(), req->limit() > 0 ? req->limit() : 1000,
                                        req->offset());
    for (const auto& obj : objects) {
        *resp->add_objects() = to_proto(obj);
    }
    resp->set_total(static_cast<uint32_t>(objects.size()));
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::UpdateChunk(grpc::ServerContext*,
                                              const metadata::UpdateChunkRequest* req,
                                              metadata::UpdateChunkResponse* resp) {
    bool ok = store_->update_chunk(req->object_id(), from_proto(req->chunk()));
    resp->set_success(ok);
    resp->set_message(ok ? "Updated" : "Failed");
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::RegisterNode(grpc::ServerContext*,
                                               const metadata::RegisterNodeRequest* req,
                                               metadata::RegisterNodeResponse* resp) {
    NodeInfo node;
    node.id = req->node_id();
    node.address = req->address();
    node.total_bytes = req->total_bytes();
    node.online = true;
    node.last_heartbeat = now_ms();
    bool ok = store_->register_node(node);
    resp->set_success(ok);
    resp->set_message(ok ? "Registered" : "Failed");
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::NodeHeartbeat(grpc::ServerContext*,
                                                const metadata::NodeHeartbeatRequest* req,
                                                metadata::NodeHeartbeatResponse* resp) {
    bool ok = store_->update_node_heartbeat(req->node_id(), req->free_bytes(), req->chunk_count());
    resp->set_success(ok);
    resp->set_message(ok ? "OK" : "Unknown node");
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::GetClusterState(grpc::ServerContext*,
                                                  const metadata::GetClusterStateRequest*,
                                                  metadata::GetClusterStateResponse* resp) {
    auto* state = resp->mutable_state();
    state->set_leader_id(store_->leader_id());
    state->set_generation(store_->generation());
    state->set_replication_factor(Config::instance().cluster().replication_factor);
    for (const auto& n : store_->get_nodes()) {
        auto* pb = state->add_nodes();
        pb->set_node_id(n.id);
        pb->set_address(n.address);
        pb->set_online(n.online);
        pb->set_free_bytes(n.free_bytes);
        pb->set_total_bytes(n.total_bytes);
        pb->set_chunk_count(n.chunk_count);
        pb->set_last_heartbeat_ms(n.last_heartbeat);
    }
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::SelectNodes(grpc::ServerContext*,
                                              const metadata::SelectNodesRequest* req,
                                              metadata::SelectNodesResponse* resp) {
    LoadBalanceStrategy strategy = Config::instance().cluster().load_balance;
    if (req->strategy() == "round_robin") strategy = LoadBalanceStrategy::RoundRobin;
    else if (req->strategy() == "random") strategy = LoadBalanceStrategy::Random;
    else if (req->strategy() == "least_used") strategy = LoadBalanceStrategy::LeastUsed;

    auto nodes = store_->select_nodes(req->count(), strategy);
    for (const auto& n : nodes) {
        auto* pb = resp->add_nodes();
        pb->set_node_id(n.id);
        pb->set_address(n.address);
        pb->set_online(n.online);
        pb->set_free_bytes(n.free_bytes);
        pb->set_total_bytes(n.total_bytes);
        pb->set_chunk_count(n.chunk_count);
    }
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::MarkNodeOffline(grpc::ServerContext*,
                                                  const metadata::MarkNodeOfflineRequest* req,
                                                  metadata::MarkNodeOfflineResponse* resp) {
    bool ok = store_->mark_node_offline(req->node_id());
    resp->set_success(ok);
    resp->set_message(ok ? "Marked offline" : "Node not found");
    return grpc::Status::OK;
}

grpc::Status MetadataServiceImpl::GetUnderReplicatedChunks(
    grpc::ServerContext*,
    const metadata::GetUnderReplicatedChunksRequest*,
    metadata::GetUnderReplicatedChunksResponse* resp) {
    auto chunks = store_->get_under_replicated_chunks(Config::instance().cluster().replication_factor);
    for (const auto& c : chunks) {
        *resp->add_chunks() = to_proto(c);
    }
    return grpc::Status::OK;
}

}  // namespace dse
