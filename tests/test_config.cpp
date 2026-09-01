#include <gtest/gtest.h>
#include "common/config.hpp"
#include <fstream>
#include <filesystem>

TEST(ConfigTest, Defaults) {
    auto& cfg = dse::Config::instance();
    EXPECT_EQ(cfg.cluster().replication_factor, 3u);
    EXPECT_EQ(cfg.cluster().chunk_size_mb, 4u);
    EXPECT_EQ(cfg.chunk_size_bytes(), 4u * 1024 * 1024);
}

TEST(ConfigTest, LoadFromFile) {
    std::ofstream f("test_config.cfg");
    f << "replication_factor=5\n";
    f << "chunk_size_mb=8\n";
    f << "node_id=test-node\n";
    f.close();

    dse::Config::instance().load_from_file("test_config.cfg");
    EXPECT_EQ(dse::Config::instance().cluster().replication_factor, 5u);
    EXPECT_EQ(dse::Config::instance().cluster().chunk_size_mb, 8u);
    EXPECT_EQ(dse::Config::instance().node_id(), "test-node");

    std::filesystem::remove("test_config.cfg");
}
