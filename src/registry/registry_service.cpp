#include "registry/registry_service.h"
#include "utils/logger.h"
#include "core/error_codes.h"

#include <cstdlib>

namespace drocm::registry {

    RegistryServiceImpl::RegistryServiceImpl(uint32_t max_nodes)
        : node_table_(max_nodes), is_running_(true) {
        DROCM_LOG_INFO("[Registry] Service initialized (max_nodes={})", max_nodes);
    }

    grpc::Status RegistryServiceImpl::Register(
        grpc::ServerContext* /* context */,
        const drocm::registry::RegisterRequest* request,
        drocm::registry::RegisterResponse* response) {

        if (!is_running_.load()) {
            DROCM_LOG_WARN("[Registry] Register rejected: service is shutting down");
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Service is shutting down");
        }

        if (!request->has_node_info()) {
            response->set_success(false);
            response->set_error_message("Missing node_info field");
            DROCM_LOG_WARN("[Registry] Register rejected: missing node_info");
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Missing node_info");
        }

        const auto& info = request->node_info();
        if (info.node_id().empty()) {
            response->set_success(false);
            response->set_error_message("node_id cannot be empty");
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Empty node_id");
        }

        // Generate a unique session ID (simple: timestamp + node_id)
        std::string session_id = info.node_id() + "_" + std::to_string(std::rand());

        bool registered = node_table_.register_node(info, session_id);
        if (!registered) {
            response->set_success(false);
            response->set_error_message("Node already registered or table full");
            DROCM_LOG_WARN("[Registry] Register failed for node {} (duplicate/full)",
                info.node_id());
            return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Duplicate node");
        }

        response->set_success(true);
        response->set_session_id(session_id);
        response->set_heartbeat_interval(1000);  // 1 second default
        response->set_error_message("");

        DROCM_LOG_INFO("[Registry] Node {} registered (IP:{}, port:{}, session:{})",
            info.node_id(), info.ip_address(), info.port(), session_id);

        return grpc::Status::OK;
    }

    grpc::Status RegistryServiceImpl::Heartbeat(
        grpc::ServerContext* /* context */,
        const drocm::registry::HeartbeatRequest* request,
        drocm::registry::HeartbeatResponse* response) {

        if (request->node_id().empty()) {
            response->set_success(false);
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Empty node_id");
        }

        bool found = node_table_.update_heartbeat(
            request->node_id(),
            request->cpu_usage(),
            request->memory_usage(),
            request->active_connections()
        );

        if (!found) {
            response->set_success(false);
            response->set_missed_heartbeats(0);
            response->set_should_reconnect(true);
            DROCM_LOG_WARN("[Registry] Heartbeat from unknown node {}",
                request->node_id());
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not registered");
        }

        response->set_success(true);
        response->set_missed_heartbeats(0);
        response->set_should_reconnect(false);

        return grpc::Status::OK;
    }

    grpc::Status RegistryServiceImpl::Deregister(
        grpc::ServerContext* /* context */,
        const drocm::registry::DeregisterRequest* request,
        drocm::registry::DeregisterResponse* response) {

        bool removed = node_table_.deregister_node(request->node_id());
        if (!removed) {
            response->set_success(false);
            response->set_message("Node not found");
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");
        }

        response->set_success(true);
        response->set_message("Node deregistered gracefully");

        return grpc::Status::OK;
    }

    grpc::Status RegistryServiceImpl::Discover(
        grpc::ServerContext* /* context */,
        const drocm::registry::DiscoverRequest* request,
        drocm::registry::DiscoverResponse* response) {

        std::vector<std::string> filter;
        for (int i = 0; i < request->service_filter_size(); ++i) {
            filter.push_back(request->service_filter(i));
        }

        auto nodes = node_table_.get_nodes_by_service(filter);

        for (const auto& entry : nodes) {
            auto* node_info = response->add_nodes();
            node_info->CopyFrom(entry.info);
        }

        DROCM_LOG_INFO("[Registry] Discover returned {} nodes (filter_size={})",
            nodes.size(), filter.size());

        return grpc::Status::OK;
    }

    NodeTable& RegistryServiceImpl::get_node_table() {
        return node_table_;
    }

    void RegistryServiceImpl::Shutdown() {
        if (!is_running_.exchange(false)) {
            DROCM_LOG_WARN("[Registry] Shutdown already called, ignoring");
            return;
        }
        DROCM_LOG_INFO("[Registry] Service shutdown initiated - rejecting new RPC requests");
    }

    bool RegistryServiceImpl::is_running() const {
        return is_running_.load();
    }

} // namespace drocm::registry
