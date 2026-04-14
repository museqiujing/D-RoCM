#pragma once

#include <coroutine>
#include <memory>
#include <grpcpp/support/async_stream.h>
#include <grpcpp/support/async_unary_call.h>

#include "coro_adapter.h"

namespace drocm::core::coro {

    /**
     * @brief Stack-allocated context for stream operations
     *
     * Lives on the Awaiter stack frame. The Awaiter outlives the coroutine
     * suspension, so poll_loop must NOT delete this context.
     */
    struct StreamContext {
        ContextType type = ContextType::STREAM_OP;
        std::coroutine_handle<> caller_handle;
        bool ok = false;
    };

    /**
     * @brief Awaits a single Read operation on a gRPC async reader
     */
    template <typename T>
    struct StreamReadAwaiter {
        grpc::ClientAsyncReaderInterface<T>* reader;
        T* msg;
        StreamContext ctx;

        StreamReadAwaiter(grpc::ClientAsyncReaderInterface<T>* r, T* m) : reader(r), msg(m) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.caller_handle = h;
            reader->Read(msg, static_cast<void*>(&ctx));
        }

        bool await_resume() const noexcept { return ctx.ok; }
    };

    /**
     * @brief Awaits a single Write operation on a gRPC async writer
     */
    template <typename T>
    struct StreamWriteAwaiter {
        grpc::ClientAsyncWriterInterface<T>* writer;
        T msg;
        StreamContext ctx;

        StreamWriteAwaiter(grpc::ClientAsyncWriterInterface<T>* w, const T& m) : writer(w), msg(m) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.caller_handle = h;
            writer->Write(msg, static_cast<void*>(&ctx));
        }

        bool await_resume() const noexcept { return ctx.ok; }
    };

    /**
     * @brief Awaits WritesDone on a bidirectional stream
     */
    template <typename T>
    struct StreamWritesDoneAwaiter {
        grpc::ClientAsyncWriterInterface<T>* writer;
        StreamContext ctx;

        StreamWritesDoneAwaiter(grpc::ClientAsyncWriterInterface<T>* w) : writer(w) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.caller_handle = h;
            writer->WritesDone(static_cast<void*>(&ctx));
        }

        bool await_resume() const noexcept { return ctx.ok; }
    };

    /**
     * @brief Awaits Finish on a stream (gets final grpc::Status)
     */
    template <typename W, typename R>
    struct StreamFinishAwaiter {
        grpc::ClientAsyncReaderWriterInterface<W, R>* stream;
        grpc::Status* status;
        StreamContext ctx;

        StreamFinishAwaiter(grpc::ClientAsyncReaderWriterInterface<W, R>* s, grpc::Status* st)
            : stream(s), status(st) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.caller_handle = h;
            stream->Finish(status, static_cast<void*>(&ctx));
        }

        grpc::Status await_resume() const noexcept { return *status; }
    };

    /**
     * @brief High-level wrapper for bidirectional async streams
     *
     * Usage:
     *   AsyncStream<RobotStatus, RobotCommand> stream = ...;
     *   RobotStatus status;
     *   while (co_await stream.read(status)) {
     *       DROCM_LOG_INFO("Got status: x={}", status.pose().x());
     *       co_await stream.write(cmd);
     *   }
     */
    template <typename R, typename W>
    class AsyncStream {
    public:
        using StreamType = grpc::ClientAsyncReaderWriterInterface<W, R>;

        explicit AsyncStream(std::unique_ptr<StreamType> stream)
            : stream_(std::move(stream)) {}

        auto read(R& msg) {
            return StreamReadAwaiter<R>(stream_.get(), &msg);
        }

        auto write(const W& msg) {
            return StreamWriteAwaiter<W>(stream_.get(), msg);
        }

        auto writes_done() {
            return StreamWritesDoneAwaiter<W>(stream_.get());
        }

        auto finish(grpc::Status& status) {
            return StreamFinishAwaiter<W, R>(stream_.get(), &status);
        }

        StreamType* get() { return stream_.get(); }

    private:
        std::unique_ptr<StreamType> stream_;
    };

} // namespace drocm::core::coro
