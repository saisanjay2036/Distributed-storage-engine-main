#pragma once

#include "chunk/chunk_format.hpp"
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dse {

struct ChunkStoreStats {
    uint64_t chunk_count{0};
    uint64_t total_bytes{0};
    uint64_t free_bytes{0};
};

class ChunkStore {
public:
    explicit ChunkStore(const std::string& base_path);

    bool write_chunk(const ChunkHeaderData& header, const std::vector<uint8_t>& payload);
    std::optional<std::pair<ChunkHeaderData, std::vector<uint8_t>>> read_chunk(const std::string& chunk_id);
    std::optional<std::vector<uint8_t>> read_chunk_range(const std::string& chunk_id, uint64_t offset, uint64_t length);
    bool delete_chunk(const std::string& chunk_id);
    bool chunk_exists(const std::string& chunk_id) const;
    bool verify_chunk(const std::string& chunk_id, std::string& checksum_out);
    ChunkStoreStats stats() const;
    std::string chunk_path(const std::string& chunk_id) const;

private:
    std::string chunk_file_path(const std::string& chunk_id) const;
    void update_stats_on_write(uint64_t size);
    void update_stats_on_delete(uint64_t size);
    uint64_t estimate_free_space() const;

    std::string base_path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint64_t> chunk_sizes_;
    uint64_t total_stored_{0};
};

}  // namespace dse
