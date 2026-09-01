#include "common/config.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace dse {

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

void Config::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::warn("Config file not found: {}, using defaults", path);
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        if (key == "replication_factor") cluster_.replication_factor = std::stoul(value);
        else if (key == "chunk_size_mb") cluster_.chunk_size_mb = std::stoul(value);
        else if (key == "heartbeat_interval_sec") cluster_.heartbeat_interval_sec = std::stoul(value);
        else if (key == "heartbeat_timeout_sec") cluster_.heartbeat_timeout_sec = std::stoul(value);
        else if (key == "metadata_address") cluster_.metadata_address = value;
        else if (key == "coordinator_address") cluster_.coordinator_address = value;
        else if (key == "node_id") node_id_ = value;
        else if (key == "storage_path") storage_path_ = value;
        else if (key == "listen_address") listen_address_ = value;
        else if (key == "advertise_address") advertise_address_ = value;
        else if (key == "metadata_persist_path") metadata_persist_path_ = value;
        else if (key == "load_balance") {
            if (value == "round_robin") cluster_.load_balance = LoadBalanceStrategy::RoundRobin;
            else if (value == "least_used") cluster_.load_balance = LoadBalanceStrategy::LeastUsed;
            else if (value == "random") cluster_.load_balance = LoadBalanceStrategy::Random;
        }
        else if (key == "write_ack") {
            if (value == "all") cluster_.write_ack = WriteAckPolicy::All;
            else cluster_.write_ack = WriteAckPolicy::Majority;
        }
        else if (key == "use_rocksdb") use_rocksdb_ = (value == "true" || value == "1");
    }
}

void Config::load_from_env() {
    if (const char* v = std::getenv("DSE_NODE_ID")) node_id_ = v;
    if (const char* v = std::getenv("DSE_STORAGE_PATH")) storage_path_ = v;
    if (const char* v = std::getenv("DSE_LISTEN_ADDRESS")) listen_address_ = v;
    if (const char* v = std::getenv("DSE_ADVERTISE_ADDRESS")) advertise_address_ = v;
    if (const char* v = std::getenv("DSE_METADATA_ADDRESS")) cluster_.metadata_address = v;
    if (const char* v = std::getenv("DSE_COORDINATOR_ADDRESS")) cluster_.coordinator_address = v;
    if (const char* v = std::getenv("DSE_REPLICATION_FACTOR")) cluster_.replication_factor = std::stoul(v);
    if (const char* v = std::getenv("DSE_CHUNK_SIZE_MB")) cluster_.chunk_size_mb = std::stoul(v);
}

}  // namespace dse
