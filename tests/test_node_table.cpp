/**
 * @file test_node_table.cpp
 * @brief Unit tests for NodeTable with health-aware service discovery
 *
 * Tests:
 * - Node registration and deregistration
 * - Health filtering in get_nodes_by_service
 * - Service name filtering
 * - Thread safety basics
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

#include "registry/node_table.h"
#include "registry.pb.h"
#include "common.pb.h"
#include "utils/logger.h"

using namespace drocm::registry;

// Global logger setup for all tests
class NodeTableTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        drocm::utils::Logger::init();
    }
    static void TearDownTestSuite() {
        drocm::utils::Logger::shutdown();
    }
};

// Helper: create a NodeInfo for testing
static drocm::common::NodeInfo make_node_info(
    const std::string& node_id,
    const std::string& ip = "127.0.0.1",
    uint32_t port = 50052,
    const std::vector<std::string>& services = { "lidar" }) {

    drocm::common::NodeInfo info;
    info.set_node_id(node_id);
    info.set_ip_address(ip);
    info.set_port(port);
    for (const auto& svc : services) {
        info.add_services(svc);
    }
    return info;
}

// ============================================================================
// Test: Basic Registration
// ============================================================================

TEST_F(NodeTableTest, RegisterAndDeregisterNode) {
    NodeTable table(100);

    auto info = make_node_info("node_01");
    EXPECT_TRUE(table.register_node(info, "session_01"));
    EXPECT_TRUE(table.has_node("node_01"));
    EXPECT_EQ(table.get_node_count(), 1u);

    EXPECT_TRUE(table.deregister_node("node_01"));
    EXPECT_FALSE(table.has_node("node_01"));
    EXPECT_EQ(table.get_node_count(), 0u);
}

TEST_F(NodeTableTest, RejectDuplicateRegistration) {
    NodeTable table(100);

    auto info = make_node_info("node_01");
    EXPECT_TRUE(table.register_node(info, "session_01"));
    EXPECT_FALSE(table.register_node(info, "session_02"));  // Duplicate
}

TEST_F(NodeTableTest, RejectRegistrationWhenTableFull) {
    NodeTable table(1);  // Max 1 node

    auto info1 = make_node_info("node_01");
    auto info2 = make_node_info("node_02");

    EXPECT_TRUE(table.register_node(info1, "session_01"));
    EXPECT_FALSE(table.register_node(info2, "session_02"));  // Table full
}

// ============================================================================
// Test: Heartbeat Updates
// ============================================================================

TEST_F(NodeTableTest, UpdateHeartbeatResetsMissedCounter) {
    NodeTable table(100);

    auto info = make_node_info("node_01");
    table.register_node(info, "session_01");

    // Simulate HealthChecker incrementing missed heartbeats
    table.increment_all_missed_heartbeats();

    auto entry = table.get_node("node_01");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->missed_heartbeats, 1u);

    // Node sends heartbeat, should reset counter
    table.update_heartbeat("node_01", 15.0f, 45.0f, 2);

    entry = table.get_node("node_01");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->missed_heartbeats, 0u);
}

// ============================================================================
// Test: Health-Aware Service Discovery (Phase 2.4 Core Feature)
// ============================================================================

TEST_F(NodeTableTest, DiscoverReturnsOnlyHealthyNodes) {
    NodeTable table(100);

    // Register 3 nodes
    table.register_node(make_node_info("healthy_lidar"), "session_01");
    table.register_node(make_node_info("unhealthy_lidar"), "session_02");
    table.register_node(make_node_info("another_healthy_lidar"), "session_03");

    // Simulate HealthChecker: increment missed heartbeats for all
    table.increment_all_missed_heartbeats();

    // healthy_lidar sends heartbeat, unhealthy_lidar doesn't
    table.update_heartbeat("healthy_lidar", 10.0f, 40.0f, 1);
    table.update_heartbeat("another_healthy_lidar", 12.0f, 42.0f, 1);

    // Discover should only return healthy nodes
    auto nodes = table.get_nodes_by_service({});
    EXPECT_EQ(nodes.size(), 2u);

    // Verify unhealthy node is filtered out
    for (const auto& entry : nodes) {
        EXPECT_NE(entry.info.node_id(), "unhealthy_lidar");
    }
}

TEST_F(NodeTableTest, DiscoverFiltersByServiceName) {
    NodeTable table(100);

    table.register_node(make_node_info("lidar_01", "127.0.0.1", 50052, { "lidar" }), "s1");
    table.register_node(make_node_info("camera_01", "127.0.0.1", 50053, { "camera" }), "s2");
    table.register_node(make_node_info("fusion_01", "127.0.0.1", 50054, { "lidar", "camera" }), "s3");

    // Filter by "lidar" service
    auto lidar_nodes = table.get_nodes_by_service({ "lidar" });
    EXPECT_EQ(lidar_nodes.size(), 2u);

    // Filter by "camera" service
    auto camera_nodes = table.get_nodes_by_service({ "camera" });
    EXPECT_EQ(camera_nodes.size(), 2u);

    // Filter by both services (OR logic)
    auto all_nodes = table.get_nodes_by_service({ "lidar", "camera" });
    EXPECT_EQ(all_nodes.size(), 3u);
}

TEST_F(NodeTableTest, DiscoverReturnsEmptyWhenNoHealthyNodes) {
    NodeTable table(100);

    table.register_node(make_node_info("node_01"), "s1");
    table.register_node(make_node_info("node_02"), "s2");

    // Simulate 3 missed heartbeats for all nodes
    table.increment_all_missed_heartbeats();
    table.increment_all_missed_heartbeats();
    table.increment_all_missed_heartbeats();

    auto nodes = table.get_nodes_by_service({});
    EXPECT_EQ(nodes.size(), 0u);
}

TEST_F(NodeTableTest, DiscoverWithEmptyFilterReturnsAllHealthy) {
    NodeTable table(100);

    table.register_node(make_node_info("node_01", "127.0.0.1", 50052, { "lidar" }), "s1");
    table.register_node(make_node_info("node_02", "127.0.0.1", 50053, { "camera" }), "s2");

    // Empty filter should return all healthy nodes
    auto nodes = table.get_nodes_by_service({});
    EXPECT_EQ(nodes.size(), 2u);
}

// ============================================================================
// Test: Stale Node Removal
// ============================================================================

TEST_F(NodeTableTest, RemoveStaleNodesRemovesUnhealthyNodes) {
    NodeTable table(100);

    table.register_node(make_node_info("healthy_node"), "s1");
    table.register_node(make_node_info("stale_node"), "s2");

    // Increment missed heartbeats
    table.increment_all_missed_heartbeats();
    table.increment_all_missed_heartbeats();
    table.increment_all_missed_heartbeats();

    // healthy_node sends heartbeat
    table.update_heartbeat("healthy_node", 10.0f, 40.0f, 1);

    auto removed = table.remove_stale_nodes(3);
    EXPECT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0], "stale_node");

    EXPECT_EQ(table.get_node_count(), 1u);
    EXPECT_TRUE(table.has_node("healthy_node"));
}

// ============================================================================
// Test: Thread Safety (Basic)
// ============================================================================

TEST_F(NodeTableTest, ConcurrentRegisterAndDiscover) {
    NodeTable table(1000);

    // Pre-register some nodes
    for (int i = 0; i < 10; ++i) {
        table.register_node(make_node_info("node_" + std::to_string(i)), "s_" + std::to_string(i));
    }

    // Run concurrent operations
    std::thread writer([&table]() {
        for (int i = 10; i < 20; ++i) {
            table.register_node(
                make_node_info("node_" + std::to_string(i)),
                "s_" + std::to_string(i));
        }
        });

    std::thread reader([&table]() {
        for (int i = 0; i < 100; ++i) {
            auto nodes = table.get_nodes_by_service({});
            // Should not crash or return corrupt data
            EXPECT_LE(nodes.size(), 20u);
        }
        });

    writer.join();
    reader.join();

    EXPECT_EQ(table.get_node_count(), 20u);
}
