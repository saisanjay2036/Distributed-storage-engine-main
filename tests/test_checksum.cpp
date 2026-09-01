#include <gtest/gtest.h>
#include "common/checksum.hpp"

TEST(ChecksumTest, EmptyData) {
    auto hash = dse::Checksum::sha256({});
    EXPECT_EQ(hash.size(), 64);
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(ChecksumTest, KnownValue) {
    std::vector<uint8_t> data = {'h', 'e', 'l', 'l', 'o'};
    auto hash = dse::Checksum::sha256(data);
    EXPECT_EQ(hash, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(ChecksumTest, Verify) {
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    auto hash = dse::Checksum::sha256(data);
    EXPECT_TRUE(dse::Checksum::verify(data, hash));
    EXPECT_FALSE(dse::Checksum::verify(data, "invalid"));
}

TEST(ChecksumTest, GenerateUuid) {
    auto id1 = dse::generate_uuid();
    auto id2 = dse::generate_uuid();
    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1.size(), 36);
}
