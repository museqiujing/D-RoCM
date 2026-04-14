#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "registry.grpc.pb.h"
#include "registry/node_table.h"

namespace drocm::registry {

    /**
     * @brief gRPC Registry Service implementation
     *
     * Handles node registration, heartbeat monitoring, and service discovery.
     * All operations use the thread-safe NodeTable with minimal lock granularity.
     *
     * Shutdown sequence (Pyramid Principle):
     * 1. Call Shutdown() to stop accepting new RPC requests
     * 2. gRPC Server should be shut down next (stops accepting new connections)
     * 3. HealthChecker thread should be stopped (triggers stop_request + join)
     * 4. Finally, RegistryService object can be destroyed
     */
    class RegistryServiceImpl final : public drocm::registry::RegistryService::Service {
    public:
        explicit RegistryServiceImpl(uint32_t max_nodes = 10000);
        ~RegistryServiceImpl() = default;

        RegistryServiceImpl(const RegistryServiceImpl&) = delete;
        RegistryServiceImpl& operator=(const RegistryServiceImpl&) = delete;

        // RPC: Register a new node
        grpc::Status Register(grpc::ServerContext* context,
            const drocm::registry::RegisterRequest* request,
            drocm::registry::RegisterResponse* response) override;

        // RPC: Send periodic heartbeat
        grpc::Status Heartbeat(grpc::ServerContext* context,
            const drocm::registry::HeartbeatRequest* request,
            drocm::registry::HeartbeatResponse* response) override;

        // RPC: Gracefully deregister a node
        grpc::Status Deregister(grpc::ServerContext* context,
            const drocm::registry::DeregisterRequest* request,
            drocm::registry::DeregisterResponse* response) override;

        // RPC: Discover nodes by service filter
        grpc::Status Discover(grpc::ServerContext* context,
            const drocm::registry::DiscoverRequest* request,
            drocm::registry::DiscoverResponse* response) override;

        /**
         * @brief Get the underlying node table for health checking
         */
        NodeTable& get_node_table();

        /**
         * @brief Gracefully shutdown the service
         *
         * Sets a flag to reject new RPC requests. Existing in-flight requests
         * will complete normally. This should be called BEFORE shutting down
         * the gRPC Server.
         */
        void Shutdown();

        /**
         * @brief Check if the service is still accepting requests
         */
        bool is_running() const;

    private:
        NodeTable node_table_;
        std::atomic<bool> is_running_;
    };

} // namespace drocm::registry
