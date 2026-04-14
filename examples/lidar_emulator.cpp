/**
 * @file lidar_emulator.cpp
 * @brief 遵循 D-RoCM 规范的 LIDAR 仿真节点
 *
 * 实现了：
 * - 指数退避自愈心跳
 * - 金字塔资源释放顺序 (AGENTS.md 红线)
 * - 信号控制保活
 * - 与 Registry 解耦，专注 Node 职责
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <signal.h>

#include <grpcpp/grpcpp.h>
#include <google/protobuf/arena.h>

#include "utils/logger.h"
#include "node.grpc.pb.h"
#include "registry.grpc.pb.h"

using namespace drocm::registry;
using namespace drocm::node;

// ============================================================================
// 全局状态控制 (符合 AGENTS.md 信号处理规范)
// ============================================================================
static std::atomic<bool> g_running{ true };
static void signal_handler(int) {
    g_running.store(false);
}

// ============================================================================
// 模拟 LIDAR 服务实现
// ============================================================================
class LidarNodeService final : public NodeService::Service {
public:
    grpc::Status GetStatus(
        grpc::ServerContext* /* ctx */,
        const drocm::node::GetStatusRequest* /* req */,
        drocm::node::GetStatusResponse* resp) override {
        resp->set_response_time(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        return grpc::Status::OK;
    }

    grpc::Status Subscribe(
        grpc::ServerContext* /* ctx */,
        const drocm::node::SubscribeRequest* request,
        grpc::ServerWriter<drocm::node::TopicData>* writer) override {

        DROCM_LOG_INFO("[LIDAR] Client subscribed to: {}", request->topic());

        for (int i = 0; i < 10 && !false; ++i) {
            google::protobuf::Arena arena;
            auto* data = google::protobuf::Arena::CreateMessage<drocm::node::TopicData>(&arena);
            data->set_topic(request->topic());
            data->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            data->set_payload_type("RobotStatus");
            data->set_payload("{\"x\":" + std::to_string(i * 0.5) + ",\"y\":" + std::to_string(i * 0.3) + "}");

            if (!writer->Write(*data)) {
                DROCM_LOG_WARN("[LIDAR] Backpressure: client too slow");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return grpc::Status::OK;
    }

    grpc::Status ControlStream(
        grpc::ServerContext* /* ctx */,
        grpc::ServerReaderWriter<drocm::node::ControlStreamResponse,
        drocm::node::ControlStreamRequest>* stream) override {

        DROCM_LOG_INFO("[LIDAR] Bidirectional stream opened");
        drocm::node::ControlStreamRequest req;
        constexpr int kMaxPending = 5;
        int pending = 0;

        while (stream->Read(&req)) {
            if (pending >= kMaxPending) {
                DROCM_LOG_WARN("[LIDAR] Backpressure: too many pending commands");
                continue;
            }
            pending++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            drocm::node::ControlStreamResponse resp;
            resp.set_session_id(req.session_id());
            if (!stream->Write(resp)) break;
            pending--;
        }
        DROCM_LOG_INFO("[LIDAR] Bidirectional stream closed");
        return grpc::Status::OK;
    }

    grpc::Status Publish(
        grpc::ServerContext* /* ctx */,
        const drocm::node::TopicData* /* req */,
        drocm::node::GetStatusResponse* /* resp */) override {
        return grpc::Status::OK;
    }
};

// ============================================================================
// 核心逻辑：自动注册 (Phase 2.4 自愈能力)
// ============================================================================
bool try_register_node(const std::string& node_id, int port, std::string& session_id) {
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    auto stub = RegistryService::NewStub(channel);

    RegisterRequest req;
    auto* info = req.mutable_node_info();
    info->set_node_id(node_id);
    info->set_ip_address("127.0.0.1");
    info->set_port(port);
    info->add_services("lidar");

    RegisterResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));

    grpc::Status status = stub->Register(&ctx, req, &resp);

    if (status.ok() && resp.success()) {
        session_id = resp.session_id();
        DROCM_LOG_INFO("[LIDAR] Re-registration success. New session: {}", session_id);
        return true;
    }
    else {
        DROCM_LOG_WARN("[LIDAR] Re-registration failed: {}", status.error_message());
        return false;
    }
}

// ============================================================================
// 核心逻辑：带指数退避的心跳与自动重注册 (Phase 2.3 + 2.4 自愈能力)
// ============================================================================
void run_heartbeat(const std::string& node_id, int port, std::string& session_id, std::atomic<bool>& heartbeat_running) {
    auto channel = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    auto stub = RegistryService::NewStub(channel);

    int consecutive_failures = 0;
    const int max_retries = 5;
    bool needs_reregister = false;

    while (heartbeat_running.load() && g_running.load()) {
        // 如果标记需要重新注册，先执行 Register RPC
        if (needs_reregister) {
            DROCM_LOG_INFO("[LIDAR] Attempting to re-register with Registry...");
            if (try_register_node(node_id, port, session_id)) {
                needs_reregister = false;
                consecutive_failures = 0;
                DROCM_LOG_INFO("[LIDAR] Re-registration successful, resuming heartbeats.");
            }
            else {
                DROCM_LOG_WARN("[LIDAR] Re-registration failed, will retry after backoff.");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
        }

        HeartbeatRequest req;
        req.set_node_id(node_id);
        req.set_session_id(session_id);
        req.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        req.set_cpu_usage(15.0f);
        req.set_memory_usage(45.0f);
        req.set_active_connections(2);

        HeartbeatResponse resp;
        grpc::ClientContext ctx;
        // 设置 500ms 超时，防止 Registry 挂死导致线程阻塞
        ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(500));

        grpc::Status status = stub->Heartbeat(&ctx, req, &resp);

        if (status.ok() && resp.success()) {
            if (consecutive_failures > 0) {
                DROCM_LOG_INFO("[LIDAR] Connection recovered after {} failures.", consecutive_failures);
            }
            consecutive_failures = 0;
            DROCM_LOG_DEBUG("[LIDAR] Heartbeat OK (missed={})", resp.missed_heartbeats());
        }
        else {
            consecutive_failures++;
            // 指数退避算法：500ms, 1s, 2s, 4s, 8s... 最大 10s
            int backoff_ms = std::min(500 * (1 << (consecutive_failures - 1)), 10000);

            DROCM_LOG_WARN("[LIDAR] Heartbeat failed ({}/{}): {}. Retrying in {}ms...",
                consecutive_failures, max_retries, status.error_message(), backoff_ms);

            if (consecutive_failures >= max_retries) {
                DROCM_LOG_ERROR("[LIDAR] Registry unreachable after {} attempts. Triggering re-registration.", max_retries);
                needs_reregister = true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    DROCM_LOG_INFO("[LIDAR] Heartbeat thread exited safely.");
}

// ============================================================================
// Main 入口
// ============================================================================
int main() {
    drocm::utils::Logger::init();
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DROCM_LOG_INFO("==========================================");
    DROCM_LOG_INFO("  D-RoCM LIDAR Node (Resilient Mode)");
    DROCM_LOG_INFO("==========================================");

    // 1. 启动节点自身的 gRPC Server (Port 50052)
    LidarNodeService lidar_service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50052", grpc::InsecureServerCredentials());
    builder.RegisterService(&lidar_service);
    auto node_server = builder.BuildAndStart();
    DROCM_LOG_INFO("[LIDAR] Node server listening on 0.0.0.0:50052");

    // 2. 向远端 Registry 注册 (仅尝试一次，失败则进入自愈循环)
    constexpr int kNodePort = 50052;
    std::string session_id = "pending";
    {
        if (try_register_node("lidar_01", kNodePort, session_id)) {
            DROCM_LOG_INFO("[LIDAR] Initial registration success. Session: {}", session_id);
        }
        else {
            DROCM_LOG_ERROR("[LIDAR] Initial registration failed. Heartbeat thread will attempt recovery.");
        }
    }

    // 3. 启动心跳线程
    std::atomic<bool> heartbeat_running{ true };
    std::thread heartbeat_thread(run_heartbeat, "lidar_01", kNodePort, std::ref(session_id), std::ref(heartbeat_running));

    // 4. 保活循环：主线程在此阻塞，直到收到 SIGINT
    DROCM_LOG_INFO("[LIDAR] Node is RUNNING. Press Ctrl+C to stop.");
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 5. 优雅关闭：遵循“金字塔”原则 (AGENTS.md 红线)
    DROCM_LOG_INFO("[LIDAR] Shutdown initiated...");

    // 5.1 先关闭 Server，停止接收新 RPC
    node_server->Shutdown();
    DROCM_LOG_INFO("[LIDAR] Server stopped accepting new requests.");

    // 5.2 停止心跳线程
    heartbeat_running.store(false);
    if (heartbeat_thread.joinable()) {
        heartbeat_thread.join();
    }

    // 5.3 等待 Server 完全退出
    node_server->Wait();

    DROCM_LOG_INFO("[LIDAR] Node exited gracefully.");
    drocm::utils::Logger::shutdown();
    return 0;
}
