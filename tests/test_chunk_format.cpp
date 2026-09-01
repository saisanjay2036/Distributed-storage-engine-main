#include <gtest/gtest.h>
#include "chunk/chunk_format.hpp"

TEST(ChunkFormatTest, SerializeDeserialize) {
    dse::ChunkHeaderData header;
    header.chunk_id = "chunk-001";
    header.object_id = "obj-001";
    header.version = 1;
    header.size = 5;
    header.checksum = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    header.created_at_ms = 1234567890;

    std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
    auto serialized = dse::ChunkFormat::serialize(header, payload);
    auto result = dse::ChunkFormat::deserialize(serialized);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.chunk_id, header.chunk_id);
    EXPECT_EQ(result->first.object_id, header.object_id);
    EXPECT_EQ(result->second, payload);
}

TEST(ChunkFormatTest, CorruptData) {
    std::vector<uint8_t> bad = {0, 0, 0, 0};
    EXPECT_FALSE(dse::ChunkFormat::deserialize(bad).has_value());
}

TEST(ChunkFormatTest, EmptyPayload) {
    dse::ChunkHeaderData header;
    header.chunk_id = "empty-chunk";
    header.object_id = "empty-obj";
    header.size = 0;
    header.checksum = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    std::vector<uint8_t> payload;
    auto serialized = dse::ChunkFormat::serialize(header, payload);
    auto result = dse::ChunkFormat::deserialize(serialized);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->second.empty());
}
