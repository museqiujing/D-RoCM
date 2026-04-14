#pragma once

#include <memory>
#include <chrono>
#include <atomic>
#include <string>
#include <mutex>
#include <unordered_map>

namespace drocm::utils {

    /**
     * @brief Lightweight metrics collector for D-RoCM observability
     *
     * Thread-safe counter-based metrics with simple HTTP exposure.
     * No external dependencies required.
     */
    class Metrics {
    public:
        static Metrics& instance();

        void increment_nodes_online();
        void decrement_nodes_online();
        void record_rpc_latency_us(double us);
        void increment_heartbeat_miss();

        // Streaming metrics (Phase 3.3)
        void increment_messages_sent();
        void record_stream_latency_ms(double ms);

        // Get metrics as Prometheus text format
        std::string get_prometheus_text() const;

    private:
        Metrics();
        ~Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

        mutable std::mutex mutex_;
        std::atomic<int64_t> nodes_online_{ 0 };
        std::atomic<int64_t> heartbeat_miss_total_{ 0 };
        std::atomic<int64_t> messages_sent_total_{ 0 };

        // Simple latency accumulators for averages
        std::atomic<int64_t> rpc_latency_sum_us_{ 0 };
        std::atomic<int64_t> rpc_latency_count_{ 0 };
        std::atomic<int64_t> stream_latency_sum_ms_{ 0 };
        std::atomic<int64_t> stream_latency_count_{ 0 };
    };

    // RAII timer for RPC latency
    struct ScopedRpcLatency {
        ScopedRpcLatency();
        ~ScopedRpcLatency();
        void stop();
    private:
        std::chrono::steady_clock::time_point start_;
        bool stopped_ = false;
    };

} // namespace drocm::utils
