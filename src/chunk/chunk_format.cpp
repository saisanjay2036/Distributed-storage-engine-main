#include "chunk/chunk_format.hpp"
#include "common/checksum.hpp"
#include <cstring>
#include <stdexcept>

namespace dse {

static void append_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<uint8_t>(v & 0xff));
}

static void append_u64(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
    }
}

static void append_string(std::vector<uint8_t>& buf, const std::string& s) {
    append_u32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

static uint32_t read_u32(const std::vector<uint8_t>& buf, size_t& pos) {
    if (pos + 4 > buf.size()) throw std::runtime_error("buffer underflow");
    uint32_t v = (static_cast<uint32_t>(buf[pos]) << 24) |
                 (static_cast<uint32_t>(buf[pos + 1]) << 16) |
                 (static_cast<uint32_t>(buf[pos + 2]) << 8) |
                 static_cast<uint32_t>(buf[pos + 3]);
    pos += 4;
    return v;
}

static uint64_t read_u64(const std::vector<uint8_t>& buf, size_t& pos) {
    if (pos + 8 > buf.size()) throw std::runtime_error("buffer underflow");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | buf[pos++];
    }
    return v;
}

static std::string read_string(const std::vector<uint8_t>& buf, size_t& pos) {
    uint32_t len = read_u32(buf, pos);
    if (pos + len > buf.size()) throw std::runtime_error("buffer underflow");
    std::string s(buf.begin() + static_cast<std::ptrdiff_t>(pos),
                  buf.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
    return s;
}

size_t ChunkFormat::header_size(const ChunkHeaderData& header) {
    return 4 + 4 + 4 + header.chunk_id.size() + 4 + header.object_id.size() +
           8 + 4 + header.checksum.size() + 8;
}

std::vector<uint8_t> ChunkFormat::header_only_bytes(const ChunkHeaderData& header) {
    std::vector<uint8_t> buf;
    buf.reserve(header_size(header));
    append_u32(buf, CHUNK_MAGIC);
    append_u32(buf, header.version);
    append_string(buf, header.chunk_id);
    append_string(buf, header.object_id);
    append_u64(buf, header.size);
    append_string(buf, header.checksum);
    append_u64(buf, header.created_at_ms);
    return buf;
}

std::vector<uint8_t> ChunkFormat::serialize(const ChunkHeaderData& header,
                                            const std::vector<uint8_t>& payload) {
    auto buf = header_only_bytes(header);
    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

std::optional<std::pair<ChunkHeaderData, std::vector<uint8_t>>>
ChunkFormat::deserialize(const std::vector<uint8_t>& data) {
    try {
        size_t pos = 0;
        uint32_t magic = read_u32(data, pos);
        if (magic != CHUNK_MAGIC) return std::nullopt;

        ChunkHeaderData header;
        header.version = read_u32(data, pos);
        header.chunk_id = read_string(data, pos);
        header.object_id = read_string(data, pos);
        header.size = read_u64(data, pos);
        header.checksum = read_string(data, pos);
        header.created_at_ms = read_u64(data, pos);

        std::vector<uint8_t> payload(data.begin() + static_cast<std::ptrdiff_t>(pos), data.end());
        if (payload.size() != header.size) return std::nullopt;
        if (!Checksum::verify(payload, header.checksum)) return std::nullopt;

        return std::make_pair(header, payload);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace dse
