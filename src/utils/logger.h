#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <memory>
#include <string_view>

namespace drocm::utils {

    /**
     * @brief High-performance asynchronous logger manager for D-RoCM
     *
     * Features:
     * - Async mode with pre-allocated ring buffer (8192 messages)
     * - Multi-sink: Colored console + daily rolling file (logs/drocm.log)
     * - Format: [timestamp] [thread_id] [level] [file:line] message
     */
    class Logger {
    public:
        /**
         * @brief Initialize the logger with async multi-sink configuration
         * @param log_dir Directory for log files (default: "logs")
         * @param async_queue_size Size of the async ring buffer (default: 8192)
         * @param async_thread_count Number of async worker threads (default: 1)
         * @return true if initialization succeeded
         */
        static bool init(
            const std::string& log_dir = "logs",
            size_t async_queue_size = 8192,
            size_t async_thread_count = 1
        );

        /**
         * @brief Get the underlying spdlog logger instance
         */
        static std::shared_ptr<spdlog::async_logger> get();

        /**
         * @brief Shutdown the logger (flush and stop async threads)
         */
        static void shutdown();

        // Non-copyable, non-movable
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

    private:
        Logger() = default;
        ~Logger() = default;

        static std::shared_ptr<spdlog::async_logger> instance_;
    };

} // namespace drocm::utils

// ============================================================================
// Logging Macros
// ============================================================================
// Usage: DROCM_LOG_INFO("message with {} placeholders", arg1, arg2)
// Automatically prepends [file:line] to the message.
// ============================================================================

#define DROCM_LOG_TRACE(fmt, ...)  \
    drocm::utils::Logger::get()->trace("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)

#define DROCM_LOG_DEBUG(fmt, ...)  \
    drocm::utils::Logger::get()->debug("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)

#define DROCM_LOG_INFO(fmt, ...)   \
    drocm::utils::Logger::get()->info("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)

#define DROCM_LOG_WARN(fmt, ...)   \
    drocm::utils::Logger::get()->warn("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)

#define DROCM_LOG_ERROR(fmt, ...)  \
    drocm::utils::Logger::get()->error("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)

#define DROCM_LOG_CRITICAL(fmt, ...) \
    drocm::utils::Logger::get()->critical("[{}:{}] " fmt, std::string(__FILE__).substr(std::string(__FILE__).find_last_of("/\\") != std::string::npos ? std::string(__FILE__).find_last_of("/\\") + 1 : 0), __LINE__, ##__VA_ARGS__)
