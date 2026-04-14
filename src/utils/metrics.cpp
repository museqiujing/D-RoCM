#include "utils/metrics.h"
#include <sstream>
#include <iomanip>

namespace drocm::utils {

    Metrics& Metrics::instance() {
        static Metrics inst;
        return inst;
    }

    Metrics::Metrics() = default;

    void Metrics::increment_nodes_online() {
        nodes_online_.fetch_add(1);
    }

    void Metrics::decrement_nodes_online() {
        nodes_online_.fetch_sub(1);
    }

    void Metrics::record_rpc_latency_us(double us) {
        rpc_latency_sum_us_.fetch_add(static_cast<int64_t>(us));
        rpc_latency_count_.fetch_add(1);
    }

    void Metrics::increment_heartbeat_miss() {
        heartbeat_miss_total_.fetch_add(1);
    }

    void Metrics::increment_messages_sent() {
        messages_sent_total_.fetch_add(1);
    }

    void Metrics::record_stream_latency_ms(double ms) {
        stream_latency_sum_ms_.fetch_add(static_cast<int64_t>(ms * 1000));  // store as us
        stream_latency_count_.fetch_add(1);
    }

    std::string Metrics::get_prometheus_text() const {
        std::ostringstream oss;

        oss << "# HELP drocm_nodes_online Current number of online nodes\n";
        oss << "# TYPE drocm_nodes_online gauge\n";
        oss << "drocm_nodes_online " << nodes_online_.load() << "\n";

        oss << "# HELP drocm_heartbeat_miss_total Total missed heartbeats\n";
        oss << "# TYPE drocm_heartbeat_miss_total counter\n";
        oss << "drocm_heartbeat_miss_total " << heartbeat_miss_total_.load() << "\n";

        oss << "# HELP drocm_messages_sent_total Total messages sent via streaming\n";
        oss << "# TYPE drocm_messages_sent_total counter\n";
        oss << "drocm_messages_sent_total " << messages_sent_total_.load() << "\n";

        auto rpc_count = rpc_latency_count_.load();
        if (rpc_count > 0) {
            double avg_us = static_cast<double>(rpc_latency_sum_us_.load()) / rpc_count;
            oss << "# HELP drocm_rpc_latency_avg_us Average RPC latency in us\n";
            oss << "# TYPE drocm_rpc_latency_avg_us gauge\n";
            oss << std::fixed << std::setprecision(2);
            oss << "drocm_rpc_latency_avg_us " << avg_us << "\n";
        }

        auto stream_count = stream_latency_count_.load();
        if (stream_count > 0) {
            double avg_ms = static_cast<double>(stream_latency_sum_ms_.load()) / stream_count / 1000.0;
            oss << "# HELP drocm_stream_latency_avg_ms Average stream latency in ms\n";
            oss << "# TYPE drocm_stream_latency_avg_ms gauge\n";
            oss << std::fixed << std::setprecision(4);
            oss << "drocm_stream_latency_avg_ms " << avg_ms << "\n";
        }

        return oss.str();
    }

    ScopedRpcLatency::ScopedRpcLatency()
        : start_(std::chrono::steady_clock::now()) {}

    ScopedRpcLatency::~ScopedRpcLatency() {
        stop();
    }

    void ScopedRpcLatency::stop() {
        if (!stopped_) {
            auto end = std::chrono::steady_clock::now();
            double us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
            Metrics::instance().record_rpc_latency_us(us);
            stopped_ = true;
        }
    }

} // namespace drocm::utils
