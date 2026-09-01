#include <gtest/gtest.h>
#include "chunk/chunk_store.hpp"
#include <filesystem>

class ChunkStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = std::filesystem::temp_directory_path() / "dse_test_store";
        std::filesystem::remove_all(path_);
        store_ = std::make_unique<dse::ChunkStore>(path_.string());
    }
    void TearDown() override { std::filesystem::remove_all(path_); }

    std::filesystem::path path_;
    std::unique_ptr<dse::ChunkStore> store_;
};

TEST_F(ChunkStoreTest, WriteReadDelete) {
    dse::ChunkHeaderData header;
    header.chunk_id = "test-chunk-1";
    header.object_id = "test-obj";
    header.size = 4;
    std::vector<uint8_t> data = {'d', 'a', 't', 'a'};

    EXPECT_TRUE(store_->write_chunk(header, data));
    EXPECT_TRUE(store_->chunk_exists("test-chunk-1"));

    auto result = store_->read_chunk("test-chunk-1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->second, data);

    std::string checksum;
    EXPECT_TRUE(store_->verify_chunk("test-chunk-1", checksum));

    EXPECT_TRUE(store_->delete_chunk("test-chunk-1"));
    EXPECT_FALSE(store_->chunk_exists("test-chunk-1"));
}

TEST_F(ChunkStoreTest, ReadRange) {
    dse::ChunkHeaderData header;
    header.chunk_id = "range-chunk";
    header.object_id = "obj";
    header.size = 10;
    std::vector<uint8_t> data = {'0','1','2','3','4','5','6','7','8','9'};
    ASSERT_TRUE(store_->write_chunk(header, data));

    auto partial = store_->read_chunk_range("range-chunk", 2, 3);
    ASSERT_TRUE(partial.has_value());
    EXPECT_EQ(partial->size(), 3);
    EXPECT_EQ((*partial)[0], '2');
}

TEST_F(ChunkStoreTest, Stats) {
    auto stats = store_->stats();
    EXPECT_EQ(stats.chunk_count, 0);
}
