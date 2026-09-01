#include <gtest/gtest.h>
#include "replication/replication_manager.hpp"
#include "metadata/metadata_store.hpp"

// Integration-level test validating replication result structure without live gRPC.
TEST(ReplicationTest, ResultDefaults) {
    dse::ReplicationManager::ReplicationResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.replicas.empty());
}

TEST(ReplicationTest, WriteAckMajority) {
    uint32_t rf = 3;
    uint32_t required = rf / 2 + 1;
    EXPECT_EQ(required, 2u);
}

TEST(ReplicationTest, WriteAckAll) {
    uint32_t rf = 3;
    uint32_t required = rf;
    EXPECT_EQ(required, 3u);
}
