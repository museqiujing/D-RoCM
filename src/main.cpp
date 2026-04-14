/**
 * @file main.cpp
 * @brief D-RoCM Entry Point
 *
 * D-RoCM (Distributed Robot Control Middleware)
 * A high-performance, observable, asynchronous communication middleware
 * for industrial robot edge computing scenarios.
 *
 * Version: 0.1.0
 */

#include "core/version.h"
#include "core/coro/coro_adapter.h"
#include "utils/logger.h"

#include <iostream>
#include <string>

int main() {
    // MUST be the first line: Logger initializes the async logging infrastructure.
    drocm::utils::Logger::init();

    DROCM_LOG_INFO("==========================================");
    DROCM_LOG_INFO("  D-RoCM - Distributed Robot Control Middleware");
    DROCM_LOG_INFO("==========================================");

    std::string version_str{ drocm::core::VersionInfo::toString() };
    DROCM_LOG_INFO("Version: {}", version_str);
    DROCM_LOG_INFO("Build: C++20 | gRPC | Protobuf");
    DROCM_LOG_INFO("==========================================");
    DROCM_LOG_INFO("{} starting", version_str);
    DROCM_LOG_DEBUG("Logger initialized with async multi-sink (console + file)");

    // Initialize coroutine adapter (Phase 1.1/1.2)
    auto coro_adapter = std::make_unique<drocm::core::coro::CoroAdapter>();
    DROCM_LOG_INFO("CoroAdapter initialized, poller thread running");

    // Verify poller is active
    if (coro_adapter->is_running()) {
        DROCM_LOG_INFO("CompletionQueue poller thread is active");
    }
    else {
        DROCM_LOG_ERROR("CoroAdapter poller thread failed to start!");
        return 1;
    }

    DROCM_LOG_INFO("Phase 0.1-0.3 & Phase 1.1/1.2 initialization complete");
    DROCM_LOG_INFO("Ready for next phases (registry, node, streaming)");

    // Graceful shutdown: logger first, then coro adapter.
    coro_adapter->shutdown();
    coro_adapter.reset();

    DROCM_LOG_INFO("D-RoCM exited cleanly");

    // Flush and shutdown logger as the absolute last operation.
    drocm::utils::Logger::shutdown();

    return 0;
}
