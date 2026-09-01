#include <gtest/gtest.h>
#include "common/types.hpp"

TEST(TypesTest, NowMs) {
    auto t1 = dse::now_ms();
    EXPECT_GT(t1, 0u);
}

TEST(TypesTest, ClusterConfigDefaults) {
    dse::ClusterConfig cfg;
    EXPECT_EQ(cfg.replication_factor, 3u);
    EXPECT_EQ(cfg.heartbeat_interval_sec, 5u);
}
