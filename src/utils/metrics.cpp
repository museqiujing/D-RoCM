#include "utils/metrics.h"

#ifdef PROMETHEUS_CPP_FOUND
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#endif

namespace drocm::utils {

    Metrics& Metrics::instance() {
        static Metrics inst;
        return inst;
    }

    Metrics::Metrics()
#ifdef PROMETHEUS_CPP_FOUND
        : registry_(std::make_shared<::prometheus::Registry>())
        , node_count_family_(&::prometheus::BuildFamily()
            .Name("drocm_nodes_online")
            .Help("Current number of online nodes")
            .Register(*registry_))
        , nodes_online_(&node_count_family_->Add({}))
        , heartbeat_miss_family_(&::prometheus::BuildFamily()
            .Name("drocm_heartbeat_miss_total")
            .Help("Total missed heartbeats")
            .Register(*registry_))
        , heartbeat_miss_total_(&heartbeat_miss_family_->Add({}))
        , rpc_latency_family_(&::prometheus::BuildFamily()
            .Name("drocm_rpc_latency_us")
            .Help("RPC latency in us")
            .Register(*registry_))
        , rpc_latency_us_(&rpc_latency_family_->Add({}, ::prometheus::Histogram::BucketBoundaries{
                  100, 500, 1000, 5000, 10000, 50000, 100000
            }))
#endif
    {
#ifdef PROMETHEUS_CPP_FOUND
        nodes_online_->Increment(0);
#endif
    }

    void Metrics::increment_nodes_online() {
#ifdef PROMETHEUS_CPP_FOUND
        if (nodes_online_) nodes_online_->Increment();
#endif
    }

    void Metrics::decrement_nodes_online() {
#ifdef PROMETHEUS_CPP_FOUND
        if (nodes_online_) nodes_online_->Decrement();
#endif
    }

    void Metrics::record_rpc_latency_us(double us) {
#ifdef PROMETHEUS_CPP_FOUND
        if (rpc_latency_us_) rpc_latency_us_->Observe(us);
#else
        (void)us;
#endif
    }

    void Metrics::increment_heartbeat_miss() {
#ifdef PROMETHEUS_CPP_FOUND
        if (heartbeat_miss_total_) heartbeat_miss_total_->Increment();
#endif
    }

#ifdef PROMETHEUS_CPP_FOUND
    std::shared_ptr<::prometheus::Registry> Metrics::get_registry() const {
        return registry_;
    }
#endif

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
