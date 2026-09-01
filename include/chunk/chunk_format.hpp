#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace dse {

// On-disk chunk format:
// [Magic: 4 bytes "DSEK"]
// [Version: 4 bytes]
// [ChunkIdLen: 4 bytes]
// [ChunkId: variable]
// [ObjectIdLen: 4 bytes]
// [ObjectId: variable]
// [PayloadSize: 8 bytes]
// [ChecksumLen: 4 bytes]
// [Checksum: variable (hex string)]
// [CreatedAt: 8 bytes]
// [Payload: variable]

constexpr uint32_t CHUNK_MAGIC = 0x4445534B;  // "DSEK"
constexpr uint32_t CHUNK_FORMAT_VERSION = 1;

struct ChunkHeaderData {
    std::string chunk_id;
    std::string object_id;
    uint32_t version{1};
    uint64_t size{0};
    std::string checksum;
    uint64_t created_at_ms{0};
};

class ChunkFormat {
public:
    static std::vector<uint8_t> serialize(const ChunkHeaderData& header, const std::vector<uint8_t>& payload);
    static std::optional<std::pair<ChunkHeaderData, std::vector<uint8_t>>> deserialize(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> header_only_bytes(const ChunkHeaderData& header);
    static size_t header_size(const ChunkHeaderData& header);
};

}  // namespace dse
