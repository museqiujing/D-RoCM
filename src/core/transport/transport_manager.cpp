#include "transport_manager.h"

namespace drocm::core::transport {

struct TransportManager::Impl {
    // Implementation details will be added in Phase 1
};

TransportManager::TransportManager(TransportConfig config)
    : config_(std::move(config)) {
}

TransportManager::~TransportManager() = default;

bool TransportManager::start() {
    // Will initialize gRPC server and channels in Phase 1
    is_running_ = true;
    return true;
}

void TransportManager::stop() {
    is_running_ = false;
}

bool TransportManager::is_running() const {
    return is_running_;
}

const TransportConfig& TransportManager::config() const {
    return config_;
}

} // namespace drocm::core::transport
