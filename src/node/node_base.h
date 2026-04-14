#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <grpcpp/channel.h>

#include "core/coro/coro_adapter.h"
#include "core/error_codes.h"
#include "node.grpc.pb.h"
#include "registry.grpc.pb.h"
#include "common.pb.h"

namespace drocm::node {

    /**
     * @brief Reconnection strategy configuration
     *
     * Implements exponential backoff for resilient reconnection.
     * Formula: delay_ms = base_delay_ms * (2 ^ attempt_count)
     */
    struct ReconnectStrategy {
        uint32_t base_delay_ms = 1000;      // Initial delay (1 second)
        uint32_t max_delay_ms = 30000;      // Maximum delay (30 seconds)
        uint32_t max_attempts = 10;         // Maximum retry attempts
        bool enable_backoff = true;         // Enable exponential backoff
    };

    /**
     * @brief Base class for D-RoCM distributed nodes
     *
     * Encapsulates:
     * - CoroAdapter for async gRPC operations
     * - gRPC Channel/Stub for Registry and Node services
     * - Node identity (ID, IP)
     * - Automatic heartbeat with exponential backoff reconnection
     *
     * Observer Pattern:
     * - Base class: Timer trigger, gRPC call encapsulation, exponential backoff logic
     * - Derived class: Provide GetNodeInfo() interface
     */
    class Node {
    public:
        Node(std::string node_id, std::string registry_address);
        virtual ~Node();

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

        /**
         * @brief Start the node: initialize CoroAdapter, register with Registry, start heartbeat
         */
        virtual bool start();

        /**
         * @brief Stop the node: deregister, stop heartbeat, shutdown CoroAdapter
         */
        virtual void stop();

        /**
         * @brief Check if node is running
         */
        bool is_running() const;

    protected:
        // Accessors for derived classes
        drocm::core::coro::CoroAdapter& get_coro_adapter();
        std::shared_ptr<grpc::Channel> get_registry_channel();
        std::unique_ptr<drocm::registry::RegistryService::Stub>& get_registry_stub();

        /**
         * @brief Derived classes MUST implement this to provide node metadata
         *
         * Called during registration and heartbeat.
         * @return NodeInfo containing node_id, ip_address, port, services
         */
        virtual drocm::common::NodeInfo GetNodeInfo() const = 0;

        /**
         * @brief Derived classes can override to customize heartbeat payload
         *
         * @return Tuple of (cpu_usage, memory_usage, active_connections)
         */
        virtual std::tuple<float, float, uint32_t> GetHeartbeatPayload() const {
            return { 0.0f, 0.0f, 0 };
        }

        /**
         * @brief Derived classes can override to handle reconnection events
         *
         * @param attempt Current attempt number (1-based)
         * @param delay_ms Delay before next attempt
         */
        virtual void OnReconnectAttempt(uint32_t /* attempt */, uint32_t /* delay_ms */) {
            // Default: no-op
        }

        /**
         * @brief Derived classes can override to handle successful reconnection
         */
        virtual void OnReconnected() {
            // Default: no-op
        }

        std::string node_id_;
        std::string registry_address_;
        std::string session_id_;  // Assigned by Registry after registration

    private:
        /**
         * @brief Background heartbeat loop with exponential backoff reconnection
         */
        void heartbeat_loop(std::stop_token stop_token);

        /**
         * @brief Calculate next delay using exponential backoff
         */
        uint32_t calculate_backoff_delay(uint32_t attempt) const;

        std::unique_ptr<drocm::core::coro::CoroAdapter> coro_adapter_;
        std::shared_ptr<grpc::Channel> registry_channel_;
        std::unique_ptr<drocm::registry::RegistryService::Stub> registry_stub_;
        std::jthread heartbeat_thread_;
        std::atomic<bool> is_running_ = false;
        std::atomic<bool> is_registered_ = false;

        // Heartbeat configuration
        uint32_t heartbeat_interval_ms_ = 1000;  // Default: 1 second
        ReconnectStrategy reconnect_strategy_;
    };

} // namespace drocm::node
