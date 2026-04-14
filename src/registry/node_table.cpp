#include "registry/node_table.h"
#include "utils/logger.h"
#include <algorithm>

namespace drocm::registry {

    NodeTable::NodeTable(uint32_t max_nodes)
        : max_nodes_(max_nodes) {}

    bool NodeTable::register_node(const drocm::common::NodeInfo& info,
        const std::string& session_id) {
        std::unique_lock lock(mutex_);

        if (nodes_.size() >= max_nodes_) {
            DROCM_LOG_WARN("[NodeTable] Table full (max={}), rejecting node {}",
                max_nodes_, info.node_id());
            return false;
        }

        auto [it, inserted] = nodes_.try_emplace(
            info.node_id(),
            NodeEntry{
                .info = info,
                .session_id = session_id,
                .last_heartbeat = std::chrono::steady_clock::now(),
                .missed_heartbeats = 0,
                .cpu_usage = 0.0f,
                .memory_usage = 0.0f
            }
        );

        if (!inserted) {
            DROCM_LOG_WARN("[NodeTable] Node {} already registered (IP:{}, port:{})",
                info.node_id(), info.ip_address(), info.port());
            return false;
        }

        DROCM_LOG_INFO("[NodeTable] Registered node {} (IP:{}, port:{}, services:{})",
            info.node_id(), info.ip_address(), info.port(),
            info.services_size());
        return true;
    }

    bool NodeTable::deregister_node(std::string_view node_id) {
        std::unique_lock lock(mutex_);
        auto it = nodes_.find(std::string(node_id));
        if (it == nodes_.end()) {
            return false;
        }

        DROCM_LOG_INFO("[NodeTable] Deregistered node {} (IP:{}, port:{})",
            it->second.info.node_id(),
            it->second.info.ip_address(),
            it->second.info.port());

        nodes_.erase(it);
        return true;
    }

    bool NodeTable::update_heartbeat(std::string_view node_id,
        float cpu_usage,
        float memory_usage,
        uint32_t /* active_connections */) {
        std::unique_lock lock(mutex_);  // Write lock because we modify state
        auto it = nodes_.find(std::string(node_id));
        if (it == nodes_.end()) {
            return false;
        }

        it->second.last_heartbeat = std::chrono::steady_clock::now();
        it->second.missed_heartbeats = 0;
        it->second.cpu_usage = cpu_usage;
        it->second.memory_usage = memory_usage;

        return true;
    }

    std::vector<NodeEntry> NodeTable::get_all_nodes() const {
        std::shared_lock lock(mutex_);
        std::vector<NodeEntry> result;
        result.reserve(nodes_.size());
        for (const auto& [id, entry] : nodes_) {
            result.push_back(entry);
        }
        return result;
    }

    std::vector<NodeEntry> NodeTable::get_nodes_by_service(
        const std::vector<std::string>& service_filter) const {

        std::shared_lock lock(mutex_);

        // Copy entries under lock, then filter outside lock
        std::vector<NodeEntry> candidates;
        candidates.reserve(nodes_.size());
        for (const auto& [id, entry] : nodes_) {
            candidates.push_back(entry);
        }

        // Filter outside the lock — no Protobuf serialization under lock
        // Key: Only return healthy nodes (missed_heartbeats == 0)
        // A node with missed_heartbeats > 0 is considered unhealthy by HealthChecker
        std::vector<NodeEntry> result;
        for (const auto& entry : candidates) {
            // Skip unhealthy nodes
            if (entry.missed_heartbeats > 0) {
                continue;
            }

            if (service_filter.empty()) {
                result.push_back(entry);
                continue;
            }

            // Filter by service names
            for (int i = 0; i < entry.info.services_size(); ++i) {
                const auto& svc = entry.info.services(i);
                if (std::find(service_filter.begin(), service_filter.end(), svc)
                    != service_filter.end()) {
                    result.push_back(entry);
                    break;
                }
            }
        }

        return result;
    }

    std::optional<NodeEntry> NodeTable::get_node(std::string_view node_id) const {
        std::shared_lock lock(mutex_);
        auto it = nodes_.find(std::string(node_id));
        if (it == nodes_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    uint32_t NodeTable::get_node_count() const {
        std::shared_lock lock(mutex_);
        return static_cast<uint32_t>(nodes_.size());
    }

    bool NodeTable::has_node(std::string_view node_id) const {
        std::shared_lock lock(mutex_);
        return nodes_.contains(std::string(node_id));
    }

    std::vector<std::string> NodeTable::remove_stale_nodes(
        uint32_t max_missed_heartbeats) {

        std::unique_lock lock(mutex_);
        std::vector<std::string> removed;

        for (auto it = nodes_.begin(); it != nodes_.end(); ) {
            if (it->second.missed_heartbeats >= max_missed_heartbeats) {
                DROCM_LOG_INFO("[NodeTable] Removing stale node {} (IP:{}, "
                    "missed_heartbeats={}, last_hb={}s ago)",
                    it->second.info.node_id(),
                    it->second.info.ip_address(),
                    it->second.missed_heartbeats,
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - it->second.last_heartbeat
                    ).count());
                removed.push_back(it->first);
                it = nodes_.erase(it);
            }
            else {
                ++it;
            }
        }

        if (!removed.empty()) {
            DROCM_LOG_WARN("[NodeTable] Removed {} stale nodes (max_missed={})",
                removed.size(), max_missed_heartbeats);
        }

        return removed;
    }

    void NodeTable::increment_all_missed_heartbeats() {
        std::unique_lock lock(mutex_);
        for (auto& [id, entry] : nodes_) {
            entry.missed_heartbeats++;
        }
    }

} // namespace drocm::registry
