#include <gtest/gtest.h>
#include "metadata/metadata_store.hpp"
#include <filesystem>

class MetadataStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = (std::filesystem::temp_directory_path() / "dse_meta_test.json").string();
        std::filesystem::remove(path_);
        store_ = std::make_unique<dse::MetadataStore>(path_);
    }
    void TearDown() override { std::filesystem::remove(path_); }

    std::string path_;
    std::unique_ptr<dse::MetadataStore> store_;
};

TEST_F(MetadataStoreTest, CreateAndLocate) {
    auto obj = store_->create_object("test.bin", 1024, 3, "user1", 4 * 1024 * 1024);
    ASSERT_TRUE(obj.has_value());
    EXPECT_EQ(obj->filename, "test.bin");
    EXPECT_EQ(obj->size, 1024);
    EXPECT_EQ(obj->chunks.size(), 1);

    auto found = store_->locate_by_filename("test.bin");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->object_id, obj->object_id);
}

TEST_F(MetadataStoreTest, DuplicateFilename) {
    ASSERT_TRUE(store_->create_object("dup.bin", 100, 3, "user", 1024 * 1024).has_value());
    EXPECT_FALSE(store_->create_object("dup.bin", 200, 3, "user", 1024 * 1024).has_value());
}

TEST_F(MetadataStoreTest, DeleteObject) {
    auto obj = store_->create_object("del.bin", 50, 3, "user", 1024 * 1024);
    ASSERT_TRUE(obj.has_value());
    EXPECT_TRUE(store_->delete_object(obj->object_id));
    EXPECT_FALSE(store_->locate_by_filename("del.bin").has_value());
}

TEST_F(MetadataStoreTest, RegisterNode) {
    dse::NodeInfo node;
    node.id = "node1";
    node.address = "localhost:50053";
    node.total_bytes = 1000000;
    EXPECT_TRUE(store_->register_node(node));

    auto nodes = store_->get_nodes();
    EXPECT_EQ(nodes.size(), 1);
    EXPECT_TRUE(nodes[0].online);
}

TEST_F(MetadataStoreTest, SelectNodes) {
    for (int i = 0; i < 3; ++i) {
        dse::NodeInfo node;
        node.id = "node" + std::to_string(i);
        node.address = "localhost:" + std::to_string(50053 + i);
        node.online = true;
        node.chunk_count = static_cast<uint64_t>(i);
        store_->register_node(node);
    }
    auto selected = store_->select_nodes(2, dse::LoadBalanceStrategy::LeastUsed);
    EXPECT_EQ(selected.size(), 2);
}

TEST_F(MetadataStoreTest, LargeFileChunking) {
    uint64_t size = 10 * 1024 * 1024;  // 10 MB
    auto obj = store_->create_object("large.bin", size, 3, "user", 4 * 1024 * 1024);
    ASSERT_TRUE(obj.has_value());
    EXPECT_EQ(obj->chunks.size(), 3);  // 4MB + 4MB + 2MB
}

TEST_F(MetadataStoreTest, EmptyFile) {
    auto obj = store_->create_object("empty.bin", 0, 3, "user", 4 * 1024 * 1024);
    ASSERT_TRUE(obj.has_value());
    EXPECT_EQ(obj->chunks.size(), 1);
    EXPECT_EQ(obj->chunks[0].size, 0);
}
