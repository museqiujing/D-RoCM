#pragma once

#include <coroutine>
#include <memory>
#include <thread>
#include <chrono>
#include <grpcpp/completion_queue.h>
#include <grpcpp/support/status.h>

#include "utils/logger.h"
#include "core/error_codes.h"

namespace drocm::core::coro {

    /**
     * @brief Type discriminator for contexts passed to CompletionQueue
     */
    enum class ContextType : uint8_t {
        ASYNC_UNARY = 0,   // Heap-allocated (AsyncContext)
        STREAM_OP = 1      // Stack-allocated (StreamContext)
    };

    /**
     * @brief AsyncContext bridges gRPC async operations with C++20 coroutines
     *
     * MEMORY SAFETY DESIGN (Raw Pointer Ownership Transfer):
     * - In await_suspend(): new AsyncContext(...) releases raw pointer to gRPC tag.
     * - In poll_loop(): static_cast<AsyncContext*> recovers the raw pointer.
     * - After resume(), delete ctx releases memory. Strict new/delete pairing.
     * - On cq_.Shutdown(): drain_remaining_tags() deletes all pending tags.
     */
    struct AsyncContext {
        ContextType type = ContextType::ASYNC_UNARY;
        std::coroutine_handle<> caller_handle;
        grpc::Status grpc_status;
        bool ok = false;
        bool timed_out = false;

        AsyncContext() = default;
        AsyncContext(const AsyncContext&) = delete;
        AsyncContext& operator=(const AsyncContext&) = delete;
    };

    /**
     * @brief Adapter that wraps gRPC CompletionQueue into a coroutine-friendly model.
     *
     * Uses std::jthread with std::stop_token for clean thread lifecycle management.
     */
    class CoroAdapter {
    public:
        CoroAdapter();
        ~CoroAdapter();

        CoroAdapter(const CoroAdapter&) = delete;
        CoroAdapter& operator=(const CoroAdapter&) = delete;
        CoroAdapter(CoroAdapter&&) = delete;
        CoroAdapter& operator=(CoroAdapter&&) = delete;

        grpc::CompletionQueue& get_completion_queue();
        void shutdown();
        bool is_running() const;

    private:
        void process_tag(void* tag, bool ok);
        void drain_remaining_tags();
        void poll_loop(std::stop_token stop_token);

        std::unique_ptr<grpc::CompletionQueue> cq_;
        std::jthread poller_thread_;
    };

    /**
     * @brief An awaitable that suspends the current coroutine until a gRPC async
     * operation completes on the CompletionQueue.
     *
     * TIMEOUT SUPPORT:
     * - If a timeout is specified and the operation does not complete in time,
     *   the coroutine is resumed with a CANCELLED status.
     */
    struct AwaitableOperation {
        CoroAdapter& adapter;
        std::function<void(void*)> start_fn;
        std::chrono::milliseconds timeout_ms{ 0 };

        // Heap-allocated AsyncContext; ownership transferred to gRPC via void* tag.
        AsyncContext* ctx;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) {
            DROCM_LOG_DEBUG("Suspending coroutine, awaiting CQ event (timeout={}ms)",
                timeout_ms.count());
            ctx->caller_handle = handle;
            ctx->timed_out = false;

            try {
                start_fn(static_cast<void*>(ctx));
            }
            catch (...) {
                DROCM_LOG_ERROR("start_fn threw exception, cleaning up AsyncContext");
                delete ctx;
                throw;
            }
        }

        grpc::Status await_resume() {
            if (ctx->timed_out) {
                DROCM_LOG_WARN("RPC operation timed out ({}ms)", timeout_ms.count());
                return grpc::Status(
                    grpc::StatusCode::DEADLINE_EXCEEDED,
                    "Operation timed out"
                );
            }
            grpc::Status status = std::move(ctx->grpc_status);
            ctx = nullptr;
            return status;
        }

        void cancel_with_timeout() {
            if (!ctx) return;
            ctx->timed_out = true;
            ctx->grpc_status = grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "Timed out");
            if (ctx->caller_handle && !ctx->caller_handle.done()) {
                ctx->caller_handle.resume();
            }
        }
    };

} // namespace drocm::core::coro
