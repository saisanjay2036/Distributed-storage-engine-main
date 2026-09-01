#pragma once

#include "common/types.hpp"
#include <string>
#include <optional>

namespace dse {

class Config {
public:
    static Config& instance();

    void load_from_file(const std::string& path);
    void load_from_env();

    const ClusterConfig& cluster() const { return cluster_; }
    ClusterConfig& cluster() { return cluster_; }

    std::string node_id() const { return node_id_; }
    void set_node_id(const std::string& id) { node_id_ = id; }

    std::string storage_path() const { return storage_path_; }
    void set_storage_path(const std::string& path) { storage_path_ = path; }

    std::string listen_address() const { return listen_address_; }
    void set_listen_address(const std::string& addr) { listen_address_ = addr; }

    std::string advertise_address() const {
        return advertise_address_.empty() ? listen_address_ : advertise_address_;
    }
    void set_advertise_address(const std::string& addr) { advertise_address_ = addr; }

    uint32_t chunk_size_bytes() const {
        return cluster_.chunk_size_mb * 1024U * 1024U;
    }

    bool use_rocksdb() const { return use_rocksdb_; }
    void set_use_rocksdb(bool v) { use_rocksdb_ = v; }

    std::string metadata_persist_path() const { return metadata_persist_path_; }
    void set_metadata_persist_path(const std::string& p) { metadata_persist_path_ = p; }

private:
    Config() = default;

    ClusterConfig cluster_;
    std::string node_id_;
    std::string storage_path_{"./storage"};
    std::string listen_address_{"0.0.0.0:50053"};
    std::string advertise_address_;
    std::string metadata_persist_path_{"./metadata_store.json"};
    bool use_rocksdb_{false};
};

}  // namespace dse
