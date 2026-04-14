#pragma once

#include <shared_mutex>
#include <unordered_map>
#include <string>
#include <chrono>
#include <optional>
#include <vector>
#include <cstdint>

#include "registry.pb.h"

namespace drocm::registry {

    /**
     * @brief Metadata for a single registered node in the Registry
     */
    struct NodeEntry {
        drocm::common::NodeInfo info;  // Node metadata (ID, IP, services)
        std::string session_id;         // Unique session ID for heartbeat validation
        std::chrono::steady_clock::time_point last_heartbeat;  // Last heartbeat timestamp
        uint32_t missed_heartbeats = 0;  // Consecutive missed heartbeats
        float cpu_usage = 0.0f;
        float memory_usage = 0.0f;
    };

    /**
     * @brief Thread-safe node table for the Registry service
     *
     * Optimized for read-heavy workloads:
     * - Register/Deregister: exclusive write lock (std::unique_lock)
     * - Discover/Heartbeat check: shared read lock (std::shared_lock)
     * - No lock is held during Protobuf serialization (done before/after locking)
     */
    class NodeTable {
    public:
        explicit NodeTable(uint32_t max_nodes = 10000);
        ~NodeTable() = default;

        NodeTable(const NodeTable&) = delete;
        NodeTable& operator=(const NodeTable&) = delete;
        NodeTable(NodeTable&&) = delete;
        NodeTable& operator=(NodeTable&&) = delete;

        /**
         * @brief Register a new node in the table
         * @return true if registration succeeded, false if node already exists or table full
         */
        bool register_node(const drocm::common::NodeInfo& info,
            const std::string& session_id);

        /**
         * @brief Remove a node from the table
         * @return true if node was found and removed
         */
        bool deregister_node(std::string_view node_id);

        /**
         * @brief Update heartbeat timestamp for a node
         * @return true if node was found
         */
        bool update_heartbeat(std::string_view node_id,
            float cpu_usage,
            float memory_usage,
            uint32_t active_connections);

        /**
         * @brief Get a snapshot of all online nodes
         * The returned vector is a COPY — no lock held by the caller.
         */
        std::vector<NodeEntry> get_all_nodes() const;

        /**
         * @brief Get nodes that provide specific services
         * @param service_filter If empty, returns all nodes
         */
        std::vector<NodeEntry> get_nodes_by_service(const std::vector<std::string>& service_filter) const;

        /**
         * @brief Get a single node's entry (optional)
         */
        std::optional<NodeEntry> get_node(std::string_view node_id) const;

        /**
         * @brief Get count of online nodes
         */
        uint32_t get_node_count() const;

        /**
         * @brief Check if a node exists in the table
         */
        bool has_node(std::string_view node_id) const;

        /**
         * @brief Remove all nodes that have exceeded the missed heartbeat threshold
         * @return List of removed node IDs
         */
        std::vector<std::string> remove_stale_nodes(uint32_t max_missed_heartbeats);

        /**
         * @brief Increment missed heartbeat count for ALL nodes
         * Called periodically by the HealthChecker timer
         */
        void increment_all_missed_heartbeats();

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, NodeEntry> nodes_;
        uint32_t max_nodes_;
    };

} // namespace drocm::registry
