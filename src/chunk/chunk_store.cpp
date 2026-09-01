#include "chunk/chunk_store.hpp"
#include "chunk/chunk_format.hpp"
#include "common/checksum.hpp"
#include "common/types.hpp"
#include <fstream>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace dse {

ChunkStore::ChunkStore(const std::string& base_path) : base_path_(base_path) {
    fs::create_directories(base_path_);
    for (const auto& entry : fs::directory_iterator(base_path_)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().string();
        if (name.size() < 10 || name.substr(name.size() - 5) != ".data") continue;

        std::ifstream file(entry.path(), std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        auto parsed = ChunkFormat::deserialize(data);
        if (parsed) {
            chunk_sizes_[parsed->first.chunk_id] = parsed->first.size;
            total_stored_ += parsed->first.size;
        }
    }
}

std::string ChunkStore::chunk_file_path(const std::string& chunk_id) const {
    return (fs::path(base_path_) / ("chunk_" + chunk_id + ".data")).string();
}

std::string ChunkStore::chunk_path(const std::string& chunk_id) const {
    return chunk_file_path(chunk_id);
}

bool ChunkStore::write_chunk(const ChunkHeaderData& header, const std::vector<uint8_t>& payload) {
    std::lock_guard lock(mutex_);

    if (payload.size() != header.size) {
        spdlog::error("Payload size mismatch for chunk {}", header.chunk_id);
        return false;
    }

    auto checksum = Checksum::sha256(payload);
    if (!header.checksum.empty() && checksum != header.checksum) {
        spdlog::error("Checksum mismatch on write for chunk {}", header.chunk_id);
        return false;
    }

    ChunkHeaderData hdr = header;
    if (hdr.checksum.empty()) hdr.checksum = checksum;
    if (hdr.created_at_ms == 0) hdr.created_at_ms = now_ms();

    auto data = ChunkFormat::serialize(hdr, payload);
    auto path = chunk_file_path(header.chunk_id);
    auto tmp_path = path + ".tmp";

    {
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            spdlog::error("Failed to open {} for writing", tmp_path);
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        if (!file) {
            fs::remove(tmp_path);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        fs::remove(tmp_path);
        return false;
    }

    if (chunk_sizes_.count(header.chunk_id)) {
        total_stored_ -= chunk_sizes_[header.chunk_id];
    }
    chunk_sizes_[header.chunk_id] = header.size;
    total_stored_ += header.size;
    return true;
}

std::optional<std::pair<ChunkHeaderData, std::vector<uint8_t>>>
ChunkStore::read_chunk(const std::string& chunk_id) {
    std::lock_guard lock(mutex_);
    auto path = chunk_file_path(chunk_id);
    if (!fs::exists(path)) return std::nullopt;

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    return ChunkFormat::deserialize(data);
}

std::optional<std::vector<uint8_t>>
ChunkStore::read_chunk_range(const std::string& chunk_id, uint64_t offset, uint64_t length) {
    auto result = read_chunk(chunk_id);
    if (!result) return std::nullopt;
    if (offset >= result->second.size()) return std::vector<uint8_t>{};
    uint64_t end = std::min(offset + length, result->second.size());
    return std::vector<uint8_t>(result->second.begin() + static_cast<std::ptrdiff_t>(offset),
                                result->second.begin() + static_cast<std::ptrdiff_t>(end));
}

bool ChunkStore::delete_chunk(const std::string& chunk_id) {
    std::lock_guard lock(mutex_);
    auto path = chunk_file_path(chunk_id);
    if (!fs::exists(path)) return false;

    if (chunk_sizes_.count(chunk_id)) {
        total_stored_ -= chunk_sizes_[chunk_id];
        chunk_sizes_.erase(chunk_id);
    }
    return fs::remove(path);
}

bool ChunkStore::chunk_exists(const std::string& chunk_id) const {
    std::lock_guard lock(mutex_);
    return fs::exists(chunk_file_path(chunk_id));
}

bool ChunkStore::verify_chunk(const std::string& chunk_id, std::string& checksum_out) {
    auto result = read_chunk(chunk_id);
    if (!result) return false;
    checksum_out = result->first.checksum;
    return Checksum::verify(result->second, result->first.checksum);
}

uint64_t ChunkStore::estimate_free_space() const {
    try {
        auto space = fs::space(base_path_);
        return space.available;
    } catch (...) {
        return 0;
    }
}

ChunkStoreStats ChunkStore::stats() const {
    std::lock_guard lock(mutex_);
    ChunkStoreStats s;
    s.chunk_count = chunk_sizes_.size();
    s.total_bytes = total_stored_;
    s.free_bytes = estimate_free_space();
    return s;
}

}  // namespace dse
