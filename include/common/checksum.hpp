#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dse {

class Checksum {
public:
    static std::string sha256(const std::vector<uint8_t>& data);
    static std::string sha256(const uint8_t* data, size_t size);
    static std::string sha256_file(const std::string& path);
    static bool verify(const std::vector<uint8_t>& data, const std::string& expected);
};

std::string generate_uuid();

}  // namespace dse
