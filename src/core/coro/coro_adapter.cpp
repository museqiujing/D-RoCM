#include "coro_adapter.h"
#include "stream_awaiters.h"
#include <mutex>

namespace drocm::core::coro {

    CoroAdapter::CoroAdapter()
        : cq_(std::make_unique<grpc::CompletionQueue>())
    {
        poller_thread_ = std::jthread([this](std::stop_token st) {
            poll_loop(st);
            });
        DROCM_LOG_INFO("CoroAdapter initialized, poller thread started");
    }

    CoroAdapter::~CoroAdapter() {
        shutdown();
    }

    void CoroAdapter::shutdown() {
        static std::once_flag shutdown_flag;
        std::call_once(shutdown_flag, [this]() {
            DROCM_LOG_INFO("Shutting down CoroAdapter poller thread");
            cq_->Shutdown();
            });
    }

    bool CoroAdapter::is_running() const {
        return poller_thread_.joinable() && !poller_thread_.get_stop_token().stop_requested();
    }

    grpc::CompletionQueue& CoroAdapter::get_completion_queue() {
        return *cq_;
    }

    /**
     * @brief Process a completed tag from the CompletionQueue.
     *
     * MEMORY SAFETY: Uses ContextType discriminator to determine ownership.
     * - ASYNC_UNARY (AsyncContext*): heap-allocated, poller owns and deletes.
     * - STREAM_OP (StreamContext*): stack-allocated, Awaiter owns, poller does NOT delete.
     */
    void CoroAdapter::process_tag(void* tag, bool ok) {
        if (!tag) {
            DROCM_LOG_WARN("Received null tag from CompletionQueue, skipping");
            return;
        }

        // Read the type discriminator from the first byte
        auto* type_ptr = static_cast<const ContextType*>(tag);
        ContextType ctx_type = *type_ptr;

        if (ctx_type == ContextType::ASYNC_UNARY) {
            // Heap-allocated AsyncContext: poller owns and must delete
            auto* ctx = static_cast<AsyncContext*>(tag);
            ctx->ok = ok;
            if (!ok) {
                ctx->grpc_status = grpc::Status(grpc::StatusCode::CANCELLED, "Async operation failed");
                DROCM_LOG_WARN("Async operation completed with failure");
            }
            else {
                ctx->grpc_status = grpc::Status::OK;
            }
            if (ctx->caller_handle && !ctx->caller_handle.done()) {
                ctx->caller_handle.resume();
            }
            delete ctx;  // Release heap memory

        }
        else if (ctx_type == ContextType::STREAM_OP) {
            // Stack-allocated StreamContext: Awaiter owns lifetime
            auto* ctx = static_cast<StreamContext*>(tag);
            ctx->ok = ok;
            if (ctx->caller_handle && !ctx->caller_handle.done()) {
                ctx->caller_handle.resume();
            }
            // DO NOT delete: lives on Awaiter stack

        }
        else {
            DROCM_LOG_ERROR("Unknown ContextType in tag: {}", static_cast<int>(ctx_type));
        }
    }

    void CoroAdapter::drain_remaining_tags() {
        void* tag = nullptr;
        bool ok = false;
        int drained = 0;

        while (true) {
            auto deadline = std::chrono::system_clock::now();
            auto status = cq_->AsyncNext(&tag, &ok, deadline);
            if (status == grpc::CompletionQueue::GOT_EVENT) {
                process_tag(tag, ok);
                drained++;
            }
            else {
                break;
            }
        }

        if (drained > 0) {
            DROCM_LOG_INFO("Drained {} remaining tags from CompletionQueue", drained);
        }
    }

    void CoroAdapter::poll_loop(std::stop_token stop_token) {
        DROCM_LOG_INFO("CoroAdapter poller thread entered main loop");

        while (!stop_token.stop_requested()) {
            void* tag = nullptr;
            bool ok = false;

            auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(100);
            auto status = cq_->AsyncNext(&tag, &ok, deadline);

            if (status == grpc::CompletionQueue::GOT_EVENT) {
                process_tag(tag, ok);
            }
            else if (status == grpc::CompletionQueue::SHUTDOWN) {
                DROCM_LOG_INFO("CompletionQueue is shut down, draining remaining tags");
                drain_remaining_tags();
                break;
            }
        }

        DROCM_LOG_INFO("CoroAdapter poller thread exited");
    }

} // namespace drocm::core::coro
