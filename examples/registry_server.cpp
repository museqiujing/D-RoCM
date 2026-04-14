/**
 * @file registry_server.cpp
 * @brief D-RoCM Registry Service (Master Node)
 *
 * Runs the central node registration and discovery service.
 * Listens for Register, Heartbeat, Deregister, and Discover RPCs.
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <signal.h>

#include <grpcpp/grpcpp.h>

#include "utils/logger.h"
#include "registry/registry_service.h"
#include "registry/health_checker.h"

static std::atomic<bool> g_running{ true };

void signal_handler(int) {
    // Signal handler must be async-signal-safe
    // Only set the flag, do NOT log here
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    drocm::utils::Logger::init();

    DROCM_LOG_INFO("==========================================");
    DROCM_LOG_INFO("  D-RoCM Registry Server (Master Node)");
    DROCM_LOG_INFO("==========================================");

    std::string bind_address = "0.0.0.0:50051";
    if (argc > 1) {
        bind_address = argv[1];
    }

    // Create Registry service and Health Checker
    auto registry_service = std::make_unique<drocm::registry::RegistryServiceImpl>();
    auto health_checker = std::make_unique<drocm::registry::HealthChecker>(
        registry_service->get_node_table(),
        1000,   // Check every 1 second
        3       // Remove after 3 missed heartbeats
    );

    // Start gRPC Server
    grpc::ServerBuilder builder;
    builder.AddListeningPort(bind_address, grpc::InsecureServerCredentials());
    builder.RegisterService(registry_service.get());
    builder.SetMaxMessageSize(64 * 1024 * 1024); // 64 MB

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    DROCM_LOG_INFO("[Registry] Server listening on {}", bind_address);

    // Start health checker background thread
    health_checker->start();

    // Setup signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Wait for shutdown signal
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    DROCM_LOG_INFO("[Registry] Shutting down...");

    // Pyramid Principle: Shutdown sequence
    // 1. Stop accepting new RPC requests
    registry_service->Shutdown();

    // 2. Shutdown gRPC Server (stops accepting new connections)
    server->Shutdown();
    server->Wait();

    // 3. Stop HealthChecker background thread
    health_checker->stop();

    // 4. RegistryService object will be destroyed automatically
    uint32_t node_count = registry_service->get_node_table().get_node_count();
    DROCM_LOG_INFO("[Registry] Server stopped. Final node count: {}", node_count);

    drocm::utils::Logger::shutdown();
    return 0;
}
