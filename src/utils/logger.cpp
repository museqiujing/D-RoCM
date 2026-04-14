#include "utils/logger.h"
#include <filesystem>
#include <spdlog/async.h>
#include <spdlog/common.h>
#include <vector>

namespace drocm::utils {

    std::shared_ptr<spdlog::async_logger> Logger::instance_ = nullptr;

    bool Logger::init(
        const std::string& log_dir,
        size_t async_queue_size,
        size_t async_thread_count
    ) {
        if (instance_) {
            // Already initialized
            return true;
        }

        // Create log directory if not exists
        std::filesystem::create_directories(log_dir);

        // Set spdlog async mode with pre-allocated ring buffer
        spdlog::init_thread_pool(async_queue_size, async_thread_count);

        // Register the async factory for spdlog::create_async
        // (already done via init_thread_pool above)

        // Sink 1: Colored console output
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");
        console_sink->set_level(spdlog::level::debug);

        // Sink 2: Daily rolling file output (rolls at midnight)
        std::string log_file = log_dir + "/drocm.log";
        auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_file, 0, 0);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] %v");
        file_sink->set_level(spdlog::level::trace);

        // Create multi-sink async logger
        std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
        instance_ = std::make_shared<spdlog::async_logger>(
            "drocm",
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );

        // Register as default logger
        spdlog::register_logger(instance_);
        spdlog::set_default_logger(instance_);

        // Set global log level
        instance_->set_level(spdlog::level::trace);

        instance_->info("Logger initialized (async mode, queue_size={}, sinks=[console, file])",
            async_queue_size, log_file);

        return true;
    }

    std::shared_ptr<spdlog::async_logger> Logger::get() {
        return instance_;
    }

    void Logger::shutdown() {
        if (!instance_) {
            return;
        }
        // Flush our logger instance directly (synchronous wait for async queue).
        instance_->flush();
        // Unregister from spdlog's global registry BEFORE shutdown.
        spdlog::drop(instance_->name());
        // Shutdown the global thread pool (only safe after all loggers are dropped).
        spdlog::shutdown();
        instance_.reset();
    }

} // namespace drocm::utils
