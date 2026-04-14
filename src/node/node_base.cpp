#include "node/node_base.h"
#include "utils/logger.h"

#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

namespace drocm::node {

    Node::Node(std::string node_id, std::string registry_address)
        : node_id_(std::move(node_id)),
        registry_address_(std::move(registry_address)),
        coro_adapter_(std::make_unique<drocm::core::coro::CoroAdapter>()) {

        DROCM_LOG_INFO("[Node] Created node {} (registry:{})", node_id_, registry_address_);
    }

    Node::~Node() {
        stop();
    }

    bool Node::start() {
        if (is_running_) {
            DROCM_LOG_WARN("[Node] Node {} already running", node_id_);
            return true;
        }

        DROCM_LOG_INFO("[Node] Starting node {}...", node_id_);

        // Initialize registry channel and stub
        registry_channel_ = grpc::CreateChannel(
            registry_address_, grpc::InsecureChannelCredentials()
        );
        registry_stub_ = drocm::registry::RegistryService::NewStub(registry_channel_);

        // Register with Registry (synchronous for simplicity)
        drocm::registry::RegisterRequest req;
        auto* info = req.mutable_node_info();
        *info = GetNodeInfo();  // Use virtual method from derived class

        drocm::registry::RegisterResponse resp;
        grpc::ClientContext ctx;

        grpc::Status status = registry_stub_->Register(&ctx, req, &resp);
        if (!status.ok() || !resp.success()) {
            DROCM_LOG_ERROR("[Node] Registration failed for node {}: {}",
                node_id_, status.error_message());
            return false;
        }

        session_id_ = resp.session_id();
        heartbeat_interval_ms_ = resp.heartbeat_interval();
        is_registered_ = true;

        DROCM_LOG_INFO("[Node] Node {} registered with Registry (session:{}, interval:{}ms)",
            node_id_, session_id_, heartbeat_interval_ms_);

        // Start heartbeat background thread
        heartbeat_thread_ = std::jthread([this](std::stop_token st) {
            heartbeat_loop(st);
            });

        is_running_ = true;
        return true;
    }

    void Node::stop() {
        if (!is_running_) {
            return;
        }

        DROCM_LOG_INFO("[Node] Stopping node {}...", node_id_);

        // Stop heartbeat thread first
        if (heartbeat_thread_.joinable()) {
            // jthread destructor will request stop + join
        }

        // Deregister from Registry if registered
        if (is_registered_) {
            drocm::registry::DeregisterRequest req;
            req.set_node_id(node_id_);

            drocm::registry::DeregisterResponse resp;
            grpc::ClientContext ctx;

            grpc::Status status = registry_stub_->Deregister(&ctx, req, &resp);
            if (status.ok() && resp.success()) {
                DROCM_LOG_INFO("[Node] Node {} deregistered from Registry", node_id_);
            }
            else {
                DROCM_LOG_WARN("[Node] Deregistration failed for node {}: {}",
                    node_id_, status.error_message());
            }
            is_registered_ = false;
        }

        // Shutdown CoroAdapter
        coro_adapter_->shutdown();
        coro_adapter_.reset();
        registry_stub_.reset();
        registry_channel_.reset();

        is_running_ = false;
        DROCM_LOG_INFO("[Node] Node {} stopped", node_id_);
    }

    bool Node::is_running() const {
        return is_running_;
    }

    drocm::core::coro::CoroAdapter& Node::get_coro_adapter() {
        return *coro_adapter_;
    }

    std::shared_ptr<grpc::Channel> Node::get_registry_channel() {
        return registry_channel_;
    }

    std::unique_ptr<drocm::registry::RegistryService::Stub>& Node::get_registry_stub() {
        return registry_stub_;
    }

    void Node::heartbeat_loop(std::stop_token stop_token) {
        DROCM_LOG_INFO("[Node] Heartbeat loop started for node {}", node_id_);

        uint32_t reconnect_attempts = 0;

        while (!stop_token.stop_requested()) {
            // Sleep for the configured interval
            std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_ms_));

            // Check stop again after sleep
            if (stop_token.stop_requested()) {
                break;
            }

            // Send heartbeat
            drocm::registry::HeartbeatRequest req;
            req.set_node_id(node_id_);
            req.set_session_id(session_id_);
            req.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

            // Get heartbeat payload from derived class
            auto [cpu, mem, conn] = GetHeartbeatPayload();
            req.set_cpu_usage(cpu);
            req.set_memory_usage(mem);
            req.set_active_connections(conn);

            drocm::registry::HeartbeatResponse resp;
            grpc::ClientContext ctx;

            grpc::Status status = registry_stub_->Heartbeat(&ctx, req, &resp);

            if (status.ok() && resp.success()) {
                // Heartbeat successful
                if (reconnect_attempts > 0) {
                    DROCM_LOG_INFO("[Node] Node {} reconnected after {} attempts",
                        node_id_, reconnect_attempts);
                    OnReconnected();
                    reconnect_attempts = 0;
                }
                DROCM_LOG_DEBUG("[Node] Heartbeat OK for node {} (missed={})",
                    node_id_, resp.missed_heartbeats());
            }
            else {
                // Heartbeat failed - trigger reconnection logic
                DROCM_LOG_WARN("[Node] Heartbeat failed for node {}: {}",
                    node_id_, status.error_message());

                if (resp.should_reconnect()) {
                    // Exponential backoff reconnection
                    if (reconnect_attempts < reconnect_strategy_.max_attempts) {
                        reconnect_attempts++;
                        uint32_t delay_ms = calculate_backoff_delay(reconnect_attempts);

                        DROCM_LOG_WARN("[Node] Reconnection attempt {}/{} for node {} (delay: {}ms)",
                            reconnect_attempts, reconnect_strategy_.max_attempts, node_id_, delay_ms);

                        OnReconnectAttempt(reconnect_attempts, delay_ms);

                        // Wait before retrying
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

                        // Try to re-register
                        drocm::registry::RegisterRequest reg_req;
                        auto* info = reg_req.mutable_node_info();
                        *info = GetNodeInfo();

                        drocm::registry::RegisterResponse reg_resp;
                        grpc::ClientContext reg_ctx;

                        grpc::Status reg_status = registry_stub_->Register(&reg_ctx, reg_req, &reg_resp);
                        if (reg_status.ok() && reg_resp.success()) {
                            session_id_ = reg_resp.session_id();
                            heartbeat_interval_ms_ = reg_resp.heartbeat_interval();
                            is_registered_ = true;
                            DROCM_LOG_INFO("[Node] Node {} re-registered (session:{})",
                                node_id_, session_id_);
                        }
                        else {
                            DROCM_LOG_ERROR("[Node] Re-registration failed for node {}: {}",
                                node_id_, reg_status.error_message());
                        }
                    }
                    else {
                        DROCM_LOG_ERROR("[Node] Max reconnection attempts ({}) reached for node {}",
                            reconnect_strategy_.max_attempts, node_id_);
                        // Give up and stop the loop
                        break;
                    }
                }
            }
        }

        DROCM_LOG_INFO("[Node] Heartbeat loop stopped for node {}", node_id_);
    }

    uint32_t Node::calculate_backoff_delay(uint32_t attempt) const {
        if (!reconnect_strategy_.enable_backoff) {
            return reconnect_strategy_.base_delay_ms;
        }

        // Exponential backoff: delay = base * (2 ^ (attempt - 1))
        uint32_t delay = reconnect_strategy_.base_delay_ms;
        for (uint32_t i = 1; i < attempt; ++i) {
            delay *= 2;
        }

        // Cap at max_delay
        return std::min(delay, reconnect_strategy_.max_delay_ms);
    }

} // namespace drocm::node
