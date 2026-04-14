#pragma once

#include <memory>
#include <string_view>

namespace drocm::core {

/**
 * @brief Version information for D-RoCM
 */
class VersionInfo {
public:
    static constexpr int MAJOR = 0;
    static constexpr int MINOR = 1;
    static constexpr int PATCH = 0;
    
    static std::string_view toString();
};

} // namespace drocm::core
