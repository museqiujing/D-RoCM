#include "registry/health_checker.h"
#include "registry/node_table.h"
#include "utils/logger.h"

#include <chrono>
#include <thread>

namespace drocm::registry {

    HealthChecker::HealthChecker(NodeTable& node_table,
        uint32_t check_interval_ms,
        uint32_t max_missed_heartbeats)
        : node_table_(node_table),
        check_interval_ms_(check_interval_ms),
        max_missed_heartbeats_(max_missed_heartbeats) {
        DROCM_LOG_INFO("[HealthChecker] Initialized (interval={}ms, max_missed={})",
            check_interval_ms, max_missed_heartbeats);
    }

    HealthChecker::~HealthChecker() {
        stop();
    }

    void HealthChecker::start() {
        if (is_running_.exchange(true)) {
            DROCM_LOG_WARN("[HealthChecker] Already running, ignoring start request");
            return;
        }

        checker_thread_ = std::jthread([this](std::stop_token st) {
            run_loop(st);
            });

        DROCM_LOG_INFO("[HealthChecker] Background thread started");
    }

    void HealthChecker::stop() {
        if (!is_running_.exchange(false)) {
            return;
        }
        DROCM_LOG_INFO("[HealthChecker] Shutting down background thread");
        // Explicitly request stop and join to ensure thread exits before destruction
        if (checker_thread_.joinable()) {
            checker_thread_.request_stop();
            checker_thread_.join();
        }
    }

    bool HealthChecker::is_running() const {
        return is_running_.load();
    }

    void HealthChecker::run_loop(std::stop_token stop_token) {
        DROCM_LOG_INFO("[HealthChecker] Entered monitoring loop");

        while (!stop_token.stop_requested()) {
            // Sleep for the configured interval
            std::this_thread::sleep_for(
                std::chrono::milliseconds(check_interval_ms_)
            );

            // Check stop again after sleep
            if (stop_token.stop_requested()) {
                break;
            }

            // Increment missed heartbeat counters for ALL nodes
            node_table_.increment_all_missed_heartbeats();

            // Remove nodes that have exceeded the threshold
            auto removed = node_table_.remove_stale_nodes(max_missed_heartbeats_);

            if (!removed.empty()) {
                for (const auto& node_id : removed) {
                    DROCM_LOG_WARN("[HealthChecker] Node {} timed out, removed from table",
                        node_id);
                }
            }

            uint32_t count = node_table_.get_node_count();
            DROCM_LOG_DEBUG("[HealthChecker] Tick complete. Online nodes: {}", count);
        }

        DROCM_LOG_INFO("[HealthChecker] Monitoring loop exited");
    }

} // namespace drocm::registry
