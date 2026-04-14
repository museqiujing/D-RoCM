#pragma once

#include <string>

namespace drocm::core {

/**
 * @brief Error codes for D-RoCM RPC operations
 *
 * Designed for fine-grained error reporting in distributed node communication.
 */
enum class ErrorCode : int {
    // Success
    OK = 0,

    // Client-side errors (4xx range)
    INVALID_ARGUMENT = 1001,     // Malformed request or missing required fields
    NODE_NOT_FOUND = 1002,       // Requested node ID does not exist in the table
    NODE_ALREADY_EXISTS = 1003,  // Attempt to register a node that is already online
    SESSION_EXPIRED = 1004,      // Heartbeat session ID is invalid or expired
    SERVICE_UNAVAILABLE = 1005,  // Registry is shutting down
    RATE_LIMITED = 1006,         // Heartbeat sent too frequently (spam protection)

    // Server-side errors (5xx range)
    INTERNAL_ERROR = 2001,       // Unexpected internal failure
    TABLE_FULL = 2002,           // Node table capacity reached (configurable limit)
    SERIALIZATION_FAILED = 2003, // Protobuf serialization error
};

/**
 * @brief Human-readable description for each error code
 */
inline const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK:                   return "OK";
        case ErrorCode::INVALID_ARGUMENT:     return "Invalid argument";
        case ErrorCode::NODE_NOT_FOUND:       return "Node not found";
        case ErrorCode::NODE_ALREADY_EXISTS:  return "Node already registered";
        case ErrorCode::SESSION_EXPIRED:      return "Session expired";
        case ErrorCode::SERVICE_UNAVAILABLE:  return "Service unavailable";
        case ErrorCode::RATE_LIMITED:         return "Rate limited";
        case ErrorCode::INTERNAL_ERROR:       return "Internal error";
        case ErrorCode::TABLE_FULL:           return "Node table full";
        case ErrorCode::SERIALIZATION_FAILED: return "Serialization failed";
        default:                              return "Unknown error";
    }
}

/**
 * @brief Result type for RPC operations (no exceptions)
 */
template<typename T>
struct Result {
    ErrorCode code = ErrorCode::INTERNAL_ERROR;
    T value{};
    std::string message;

    static Result ok(T val, std::string msg = "") {
        return Result{ErrorCode::OK, std::move(val), std::move(msg)};
    }

    static Result error(ErrorCode code, std::string msg) {
        return Result{code, T{}, std::move(msg)};
    }

    bool is_ok() const { return code == ErrorCode::OK; }
};

// Specialization for void
template<>
struct Result<void> {
    ErrorCode code = ErrorCode::INTERNAL_ERROR;
    std::string message;

    static Result ok(std::string msg = "") {
        return Result{ErrorCode::OK, std::move(msg)};
    }

    static Result error(ErrorCode code, std::string msg) {
        return Result{code, std::move(msg)};
    }

    bool is_ok() const { return code == ErrorCode::OK; }
};

} // namespace drocm::core
