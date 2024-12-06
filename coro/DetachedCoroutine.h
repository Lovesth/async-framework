#pragma once

#include <stdio.h>
#include <exception>
#include <coroutine>

namespace async_framework
{
    namespace coro
    {
        namespace detail
        {
            // A detached coroutine. It would start to execute
            // immediately and throws the exception it met.
            // This could be used as the root of a coroutine
            // execution chain.
            //
            // But the user shouldn't use this directly. It may be
            // better to use `Lazy::start()`.
            struct DetachedCoroutine
            {
                struct promise_type
                {
                    std::suspend_never initial_suspend() noexcept { return {}; }
                    std::suspend_never final_suspend() noexcept { return {}; }
                    void return_void() noexcept {}
                    void unhandled_exception()
                    {
                        try
                        {
                            std::rethrow_exception(std::current_exception());
                        }
                        catch (const std::exception &e)
                        {
                            fprintf(stderr, "find exception %s\n", e.what());
                            fflush(stderr);
                            std::rethrow_exception(std::current_exception());
                        }
                    }
                    DetachedCoroutine get_return_object() noexcept
                    {
                        return DetachedCoroutine();
                    }

                    // Hint to gdb script for that there is no continuation for
                    // DetachedCoroutine.
                    std::coroutine_handle<> continuation_ = nullptr;
                    void *lazy_local_ = nullptr;
                };
            };
        } // namespace detail

        // This allows we to co_await a non-awaitable. It would make
        // the co_await expression to return directly.
        template <typename T>
        struct ReadyAwaiter
        {
            ReadyAwaiter(T value) : value_(std::move(value)) {}
            bool await_ready() const noexcept { return true; }
            void await_suspend(std::coroutine_handle<>) const noexcept {};
            T await_resume() noexcept { return std::move(value_); }
            T value_;
        };
    } // namespace coro
} // namespace async_framework