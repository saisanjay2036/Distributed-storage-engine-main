#include "client/storage_client.hpp"
#include "network/grpc_util.hpp"
#include "common/config.hpp"
#include "common/checksum.hpp"
#include "storage.grpc.pb.h"
#include <fstream>
#include <spdlog/spdlog.h>

namespace dse {

StorageClient::StorageClient(const std::string& metadata_address)
    : metadata_address_(metadata_address) {
    metadata_stub_ = metadata::MetadataService::NewStub(create_channel(metadata_address_));
}

bool StorageClient::write_chunk_to_node(const std::string& address, const ChunkInfo& chunk,
                                        const std::vector<uint8_t>& data,
                                        const std::string& object_id) {
    auto stub = storage::StorageService::NewStub(create_channel(address));
    storage::WriteChunkRequest req;
    auto* h = req.mutable_header();
    h->set_chunk_id(chunk.chunk_id);
    h->set_object_id(object_id);
    h->set_version(chunk.version);
    h->set_size(data.size());
    h->set_checksum(Checksum::sha256(data));
    req.set_data(data.data(), data.size());

    storage::WriteChunkResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
    return stub->WriteChunk(&ctx, req, &resp).ok() && resp.success();
}

std::optional<std::vector<uint8_t>>
StorageClient::read_chunk_from_replicas(const ChunkInfo& chunk) {
    for (const auto& rep : chunk.replicas) {
        auto stub = storage::StorageService::NewStub(create_channel(rep.address));
        storage::ReadChunkRequest req;
        req.set_chunk_id(chunk.chunk_id);
        storage::ReadChunkResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
        if (stub->ReadChunk(&ctx, req, &resp).ok() && resp.success()) {
            if (!chunk.checksum.empty() && resp.header().checksum() != chunk.checksum) {
                spdlog::warn("Checksum mismatch on chunk {}, trying next replica", chunk.chunk_id);
                continue;
            }
            return std::vector<uint8_t>(resp.data().begin(), resp.data().end());
        }
    }
    return std::nullopt;
}

PutResult StorageClient::put(const std::string& filename, const std::vector<uint8_t>& data,
                             const std::string& owner) {
    PutResult result;
    auto& cfg = Config::instance().cluster();

    metadata::CreateObjectRequest create_req;
    create_req.set_filename(filename);
    create_req.set_size(data.size());
    create_req.set_replication_factor(cfg.replication_factor);
    create_req.set_owner(owner);
    create_req.set_chunk_size_mb(cfg.chunk_size_mb);

    metadata::CreateObjectResponse create_resp;
    grpc::ClientContext ctx;
    if (!metadata_stub_->CreateObject(&ctx, create_req, &create_resp).ok() || !create_resp.success()) {
        result.message = create_resp.message().empty() ? "CreateObject failed" : create_resp.message();
        return result;
    }

    const auto& obj = create_resp.object();
    result.object_id = obj.object_id();

    uint64_t offset = 0;
    std::vector<std::future<bool>> futures;

    for (const auto& chunk_pb : obj.chunks()) {
        uint64_t chunk_size = chunk_pb.size();
        std::vector<uint8_t> chunk_data;
        if (chunk_size > 0) {
            chunk_data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                              data.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
        }
        offset += chunk_size;

        ChunkInfo chunk;
        chunk.chunk_id = chunk_pb.chunk_id();
        chunk.object_id = obj.object_id();
        chunk.chunk_index = chunk_pb.chunk_index();
        chunk.size = chunk_size;
        chunk.checksum = Checksum::sha256(chunk_data);
        chunk.version = 1;

        metadata::SelectNodesRequest sel_req;
        sel_req.set_count(cfg.replication_factor);
        metadata::SelectNodesResponse sel_resp;
        grpc::ClientContext sel_ctx;
        if (!metadata_stub_->SelectNodes(&sel_ctx, sel_req, &sel_resp).ok()) {
            result.message = "Node selection failed";
            return result;
        }

        uint32_t success_count = 0;
        bool primary_set = false;
        for (const auto& node : sel_resp.nodes()) {
            if (write_chunk_to_node(node.address(), chunk, chunk_data, obj.object_id())) {
                ++success_count;
                ReplicaInfo rep;
                rep.node_id = node.node_id();
                rep.address = node.address();
                rep.is_primary = !primary_set;
                rep.version = 1;
                primary_set = true;
                chunk.replicas.push_back(rep);
            }
        }

        uint32_t required = (cfg.write_ack == WriteAckPolicy::All)
                                ? cfg.replication_factor
                                : (cfg.replication_factor / 2 + 1);
        if (success_count < required) {
            result.message = "Failed to achieve replication quorum for chunk " + chunk.chunk_id;
            metadata::DeleteObjectRequest del_req;
            del_req.set_object_id(obj.object_id());
            metadata::DeleteObjectResponse del_resp;
            grpc::ClientContext del_ctx;
            metadata_stub_->DeleteObject(&del_ctx, del_req, &del_resp);
            return result;
        }

        metadata::UpdateChunkRequest upd_req;
        upd_req.set_object_id(obj.object_id());
        auto* pb = upd_req.mutable_chunk();
        pb->set_chunk_id(chunk.chunk_id);
        pb->set_object_id(chunk.object_id);
        pb->set_chunk_index(chunk.chunk_index);
        pb->set_size(chunk.size);
        pb->set_checksum(chunk.checksum);
        pb->set_version(1);
        for (const auto& r : chunk.replicas) {
            auto* rep_pb = pb->add_replicas();
            rep_pb->set_node_id(r.node_id);
            rep_pb->set_address(r.address);
            rep_pb->set_is_primary(r.is_primary);
            rep_pb->set_version(r.version);
        }
        metadata::UpdateChunkResponse upd_resp;
        grpc::ClientContext upd_ctx;
        metadata_stub_->UpdateChunk(&upd_ctx, upd_req, &upd_resp);
    }

    result.success = true;
    result.message = "Upload complete";
    return result;
}

PutResult StorageClient::put_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return {false, "", "Cannot open file: " + filepath};
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    auto pos = filepath.find_last_of("/\\");
    std::string filename = (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);
    return put(filename, data);
}

GetResult StorageClient::get(const std::string& filename) {
    GetResult result;
    metadata::LocateObjectRequest req;
    req.set_filename(filename);
    metadata::LocateObjectResponse resp;
    grpc::ClientContext ctx;
    if (!metadata_stub_->LocateObject(&ctx, req, &resp).ok() || !resp.success()) {
        result.message = "Object not found";
        return result;
    }

    const auto& obj = resp.object();
    result.metadata.object_id = obj.object_id();
    result.metadata.filename = obj.filename();
    result.metadata.size = obj.size();
    result.metadata.chunks.resize(obj.chunks_size());

    for (int i = 0; i < obj.chunks_size(); ++i) {
        const auto& cp = obj.chunks(i);
        ChunkInfo chunk;
        chunk.chunk_id = cp.chunk_id();
        chunk.object_id = cp.object_id();
        chunk.chunk_index = cp.chunk_index();
        chunk.size = cp.size();
        chunk.checksum = cp.checksum();
        chunk.version = cp.version();
        for (const auto& r : cp.replicas()) {
            ReplicaInfo rep;
            rep.node_id = r.node_id();
            rep.address = r.address();
            rep.is_primary = r.is_primary();
            rep.version = r.version();
            chunk.replicas.push_back(rep);
        }

        auto data = read_chunk_from_replicas(chunk);
        if (!data) {
            result.message = "Failed to read chunk " + chunk.chunk_id;
            return result;
        }
        result.data.insert(result.data.end(), data->begin(), data->end());
    }

    result.success = true;
    result.message = "Download complete";
    return result;
}

GetResult StorageClient::get_file(const std::string& filename, const std::string& output_path) {
    auto result = get(filename);
    if (!result.success) return result;
    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        result.success = false;
        result.message = "Cannot write to " + output_path;
        return result;
    }
    file.write(reinterpret_cast<const char*>(result.data.data()),
               static_cast<std::streamsize>(result.data.size()));
    return result;
}

bool StorageClient::delete_object(const std::string& filename) {
    metadata::LocateObjectRequest loc_req;
    loc_req.set_filename(filename);
    metadata::LocateObjectResponse loc_resp;
    grpc::ClientContext loc_ctx;
    if (!metadata_stub_->LocateObject(&loc_ctx, loc_req, &loc_resp).ok() || !loc_resp.success()) {
        return false;
    }

    for (const auto& chunk : loc_resp.object().chunks()) {
        for (const auto& rep : chunk.replicas()) {
            auto stub = storage::StorageService::NewStub(create_channel(rep.address()));
            storage::DeleteChunkRequest del_req;
            del_req.set_chunk_id(chunk.chunk_id());
            storage::DeleteChunkResponse del_resp;
            grpc::ClientContext ctx;
            stub->DeleteChunk(&ctx, del_req, &del_resp);
        }
    }

    metadata::DeleteObjectRequest req;
    req.set_filename(filename);
    metadata::DeleteObjectResponse resp;
    grpc::ClientContext ctx;
    return metadata_stub_->DeleteObject(&ctx, req, &resp).ok() && resp.success();
}

std::vector<ObjectInfo> StorageClient::list(const std::string& prefix) {
    metadata::ListObjectsRequest req;
    req.set_prefix(prefix);
    req.set_limit(10000);
    metadata::ListObjectsResponse resp;
    grpc::ClientContext ctx;
    std::vector<ObjectInfo> result;
    if (!metadata_stub_->ListObjects(&ctx, req, &resp).ok()) return result;

    for (const auto& obj : resp.objects()) {
        ObjectInfo info;
        info.object_id = obj.object_id();
        info.filename = obj.filename();
        info.size = obj.size();
        info.replication_factor = obj.replication_factor();
        info.version = obj.version();
        info.owner = obj.owner();
        info.created_at = obj.created_at_ms();
        info.updated_at = obj.updated_at_ms();
        result.push_back(info);
    }
    return result;
}

std::optional<ObjectInfo> StorageClient::head(const std::string& filename) {
    metadata::LocateObjectRequest req;
    req.set_filename(filename);
    metadata::LocateObjectResponse resp;
    grpc::ClientContext ctx;
    if (!metadata_stub_->LocateObject(&ctx, req, &resp).ok() || !resp.success()) {
        return std::nullopt;
    }
    ObjectInfo info;
    const auto& obj = resp.object();
    info.object_id = obj.object_id();
    info.filename = obj.filename();
    info.size = obj.size();
    info.replication_factor = obj.replication_factor();
    info.version = obj.version();
    info.checksum = obj.checksum();
    info.owner = obj.owner();
    return info;
}

}  // namespace dse
