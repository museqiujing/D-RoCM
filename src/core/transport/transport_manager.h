#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace drocm::core::transport {

/**
 * @brief Configuration for transport layer
 * 
 * Holds all configurable parameters for network transport.
 * No hardcoded values - all timeouts and ports are configurable.
 */
struct TransportConfig {
    std::string server_address = "0.0.0.0";
    uint32_t server_port = 50051;
    
    // Timeout settings in milliseconds
    uint32_t connection_timeout_ms = 5000;
    uint32_t request_timeout_ms = 3000;
    
    // Retry settings
    uint32_t max_retries = 3;
    uint32_t retry_backoff_base_ms = 100;
    
    // Keepalive settings
    uint32_t keepalive_time_ms = 10000;
    uint32_t keepalive_timeout_ms = 5000;
};

/**
 * @brief Transport layer manager
 * 
 * Manages gRPC channels and servers for node communication.
 * Currently a stub, will be implemented in Phase 1.
 */
class TransportManager {
public:
    explicit TransportManager(TransportConfig config = TransportConfig{});
    ~TransportManager();
    
    // Non-copyable
    TransportManager(const TransportManager&) = delete;
    TransportManager& operator=(const TransportManager&) = delete;
    
    // Non-movable (owns resources)
    TransportManager(TransportManager&&) = delete;
    TransportManager& operator=(TransportManager&&) = delete;
    
    // Start the transport
    bool start();
    
    // Stop the transport
    void stop();
    
    // Check if running
    bool is_running() const;
    
    // Get configuration
    const TransportConfig& config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TransportConfig config_;
    bool is_running_ = false;
};

} // namespace drocm::core::transport
