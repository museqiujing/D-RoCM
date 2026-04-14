#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace drocm::registry {

class NodeTable;

/**
 * @brief Background health checker that monitors node heartbeats
 *
 * Runs on a dedicated std::jthread with std::stop_token.
 * Periodically increments all nodes' missed heartbeat counters,
 * then removes nodes that exceed the threshold (default: 3 missed = 3s offline).
 */
class HealthChecker {
public:
    /**
     * @param node_table Reference to the shared NodeTable
     * @param check_interval_ms How often to run the check (default: 1000ms)
     * @param max_missed_heartbeats Threshold before a node is considered stale (default: 3)
     */
    HealthChecker(NodeTable& node_table,
                  uint32_t check_interval_ms = 1000,
                  uint32_t max_missed_heartbeats = 3);

    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    /**
     * @brief Start the health checker background thread
     */
    void start();

    /**
     * @brief Stop the health checker gracefully
     */
    void stop();

    /**
     * @brief Check if the checker is running
     */
    bool is_running() const;

private:
    /**
     * @brief Background loop: sleep → increment → check → repeat
     */
    void run_loop(std::stop_token stop_token);

    NodeTable& node_table_;
    std::jthread checker_thread_;
    std::atomic<bool> is_running_ = false;
    uint32_t check_interval_ms_;
    uint32_t max_missed_heartbeats_;
};

} // namespace drocm::registry
