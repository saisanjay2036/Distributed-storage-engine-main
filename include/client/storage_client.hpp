#pragma once

#include "common/types.hpp"
#include "common/thread_pool.hpp"
#include "metadata.grpc.pb.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dse {

struct PutResult {
    bool success{false};
    std::string object_id;
    std::string message;
};

struct GetResult {
    bool success{false};
    std::vector<uint8_t> data;
    ObjectInfo metadata;
    std::string message;
};

class StorageClient {
public:
    explicit StorageClient(const std::string& metadata_address);

    PutResult put(const std::string& filename, const std::vector<uint8_t>& data,
                  const std::string& owner = "default");
    PutResult put_file(const std::string& filepath);
    GetResult get(const std::string& filename);
    GetResult get_file(const std::string& filename, const std::string& output_path);
    bool delete_object(const std::string& filename);
    std::vector<ObjectInfo> list(const std::string& prefix = "");
    std::optional<ObjectInfo> head(const std::string& filename);

private:
    bool write_chunk_to_node(const std::string& address, const ChunkInfo& chunk,
                             const std::vector<uint8_t>& data, const std::string& object_id);
    std::optional<std::vector<uint8_t>> read_chunk_from_replicas(const ChunkInfo& chunk);

    std::string metadata_address_;
    std::unique_ptr<metadata::MetadataService::Stub> metadata_stub_;
    ThreadPool pool_{4};
};

}  // namespace dse
