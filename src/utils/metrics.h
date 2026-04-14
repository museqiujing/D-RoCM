#pragma once

#include <memory>
#include <chrono>

#ifdef PROMETHEUS_CPP_FOUND
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#endif

namespace drocm::utils {

    /**
     * @brief Metrics collector for D-RoCM observability
     *
     * Wraps prometheus-cpp if available, otherwise acts as a stub.
     * Thread-safe and low-overhead.
     */
    class Metrics {
    public:
        static Metrics& instance();

        void increment_nodes_online();
        void decrement_nodes_online();
        void record_rpc_latency_us(double us);
        void increment_heartbeat_miss();

#ifdef PROMETHEUS_CPP_FOUND
        std::shared_ptr<::prometheus::Registry> get_registry() const;
#endif

    private:
        Metrics();
        ~Metrics() = default;
        Metrics(const Metrics&) = delete;
        Metrics& operator=(const Metrics&) = delete;

#ifdef PROMETHEUS_CPP_FOUND
        std::shared_ptr<::prometheus::Registry> registry_;
        ::prometheus::Family<::prometheus::Counter>* node_count_family_;
        ::prometheus::Counter* nodes_online_;
        ::prometheus::Family<::prometheus::Counter>* heartbeat_miss_family_;
        ::prometheus::Counter* heartbeat_miss_total_;
        ::prometheus::Family<::prometheus::Histogram>* rpc_latency_family_;
        ::prometheus::Histogram* rpc_latency_us_;
#endif
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
