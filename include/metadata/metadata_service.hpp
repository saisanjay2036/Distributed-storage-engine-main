#pragma once

#include "metadata/metadata_store.hpp"
#include "metadata.grpc.pb.h"
#include "metadata.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>

namespace dse {

class MetadataServiceImpl final : public metadata::MetadataService::Service {
public:
    explicit MetadataServiceImpl(std::shared_ptr<MetadataStore> store);

    grpc::Status CreateObject(grpc::ServerContext* ctx,
                              const metadata::CreateObjectRequest* req,
                              metadata::CreateObjectResponse* resp) override;
    grpc::Status DeleteObject(grpc::ServerContext* ctx,
                              const metadata::DeleteObjectRequest* req,
                              metadata::DeleteObjectResponse* resp) override;
    grpc::Status LocateObject(grpc::ServerContext* ctx,
                              const metadata::LocateObjectRequest* req,
                              metadata::LocateObjectResponse* resp) override;
    grpc::Status ListObjects(grpc::ServerContext* ctx,
                             const metadata::ListObjectsRequest* req,
                             metadata::ListObjectsResponse* resp) override;
    grpc::Status UpdateChunk(grpc::ServerContext* ctx,
                             const metadata::UpdateChunkRequest* req,
                             metadata::UpdateChunkResponse* resp) override;
    grpc::Status RegisterNode(grpc::ServerContext* ctx,
                              const metadata::RegisterNodeRequest* req,
                              metadata::RegisterNodeResponse* resp) override;
    grpc::Status NodeHeartbeat(grpc::ServerContext* ctx,
                               const metadata::NodeHeartbeatRequest* req,
                               metadata::NodeHeartbeatResponse* resp) override;
    grpc::Status GetClusterState(grpc::ServerContext* ctx,
                                 const metadata::GetClusterStateRequest* req,
                                 metadata::GetClusterStateResponse* resp) override;
    grpc::Status SelectNodes(grpc::ServerContext* ctx,
                             const metadata::SelectNodesRequest* req,
                             metadata::SelectNodesResponse* resp) override;
    grpc::Status MarkNodeOffline(grpc::ServerContext* ctx,
                                 const metadata::MarkNodeOfflineRequest* req,
                                 metadata::MarkNodeOfflineResponse* resp) override;
    grpc::Status GetUnderReplicatedChunks(grpc::ServerContext* ctx,
                                          const metadata::GetUnderReplicatedChunksRequest* req,
                                          metadata::GetUnderReplicatedChunksResponse* resp) override;

    std::shared_ptr<MetadataStore> store() const { return store_; }

private:
    static metadata::ObjectMetadata to_proto(const ObjectInfo& obj);
    static metadata::ChunkMetadata to_proto(const ChunkInfo& chunk);
    static ChunkInfo from_proto(const metadata::ChunkMetadata& chunk);

    std::shared_ptr<MetadataStore> store_;
};

}  // namespace dse
