#pragma once

#include "../Executor.h"
#include "./Traits.h"
#include <cassert>
#include <utility>

namespace async_framework
{
    namespace coro
    {
        namespace detail
        {
            class ViaCoroutine
            {
            public:
                struct promise_type
                {
                    struct FinalAwaiter;
                    promise_type(Executor *ex) : ex_(ex), ctx_(Executor::NULLCTX) {}
                    ViaCoroutine get_return_object() noexcept;
                    void return_void() noexcept {}
                    void unhandled_exception() const noexcept { assert(false); }

                    std::suspend_always initial_suspend() const noexcept { return {}; }
                    FinalAwaiter final_suspend() noexcept { return FinalAwaiter(ctx_); }
                    struct FinalAwaiter
                    {
                        FinalAwaiter(Executor::Context ctx) : ctx_(ctx_) {}
                        bool await_ready() const noexcept { return false; }

                        template <typename PromiseType>
                        auto await_suspend(std::coroutine_handle<PromiseType> h) noexcept
                        {
                            auto &pr = h.promise();
                            if (pr.ex_)
                            {
                                pr.ex_->checkin(pr.continuation_, ctx_);
                            }
                            else
                            {
                                pr.continuation_.resume();
                            }
                        }
                        void await_resume() const noexcept {}
                        Executor::Context ctx_;
                    };
                    /// IMPORTANT: _continuation should be the first member due to the
                    /// requirement of dbg script.
                    std::coroutine_handle<> continuation_;
                    Executor *ex_;
                    Executor::Context ctx_;
                };

                ViaCoroutine(std::coroutine_handle<promise_type> coro) : coro_(coro) {}
                ~ViaCoroutine()
                {
                    if (coro_)
                    {
                        coro_.destroy();
                        coro_ = nullptr;
                    }
                }

                ViaCoroutine(const ViaCoroutine &) = delete;
                ViaCoroutine &operator=(const ViaCoroutine &) = delete;
                ViaCoroutine(ViaCoroutine &&other) : coro_(std::exchange(other.coro_, nullptr)) {}

            public:
                static ViaCoroutine create([[maybe_unused]] Executor *ex) { co_return; }

            public:
                void checkin()
                {
                    auto &pr = coro_.promise();
                    if (pr.ex_)
                    {
                        std::function<void()> func = []() {};
                        pr.ex_->checkin(func, pr.ctx_);
                    }
                }

                std::coroutine_handle<> getWrappedContinuation(std::coroutine_handle<> continuation)
                {
                    // do not call this method on a moved ViaCoroutine,
                    assert(coro_);
                    auto &pr = coro_.promise();
                    if (pr.ex_)
                    {
                        pr.ctx_ = pr.ex_->checkout();
                    }
                    pr.continuation_ = continuation;
                }

            private:
                std::coroutine_handle<promise_type> coro_;
            };

            inline ViaCoroutine ViaCoroutine::promise_type::get_return_object() noexcept
            {
                return ViaCoroutine(std::coroutine_handle<ViaCoroutine::promise_type>::from_promise(*this));
            }

            // used by co_await non-Lazy object
            template <typename Awaiter>
            struct [[nodiscard]] ViaAsyncAwaiter
            {
                template <typename Awaitable>
                ViaAsyncAwaiter(Executor *ex, Awaitable &&awaitable) : ex_(ex), awaiter_(detail::getAwaiter(std::forward<Awaitable>(awaitable))),
                                                                       viaCoroutine_(ViaCoroutine::create(ex)) {}

                using HandleType = std::coroutine_handle<>;
                using AwaitSuspendResultType = decltype(std::declval<Awaiter &>().await_suspend(std::declval<HandleType>()));
                bool await_ready() { return awaiter_.await_ready(); }

                AwaitSuspendResultType await_suspend(HandleType continuation)
                {
                    if constexpr (std::is_same_v<AwaitSuspendResultType, bool>)
                    {
                        bool should_suspend = awaiter_.await_suspend(viaCoroutine_.getWrappedContinuation(continuation););
                        // TODO: if should_suspend is false, checkout/checkin should not be
                        // called.
                        if (should_suspend == false)
                        {
                            viaCoroutine_.checkin();
                        }
                        return should_suspend;
                    }
                    else
                    {
                        return awaiter_.await_suspend(viaCoroutine_.getWrappedContinuation(continuation));
                    }
                }

                auto await_resume()
                {
                    return awaiter_.await_resume();
                }

                Executor *ex_;
                Awaiter awaiter_;
                ViaCoroutine viaCoroutine_;
            };
            // While co_await Awaitable in a Lazy coroutine body:
            //  1. Awaitable has no "coAwait" method: a ViaAsyncAwaiter is created, current
            // coroutine_handle will be wrapped into a ViaCoroutine. Reschedule will happen
            // when resume from a ViaCoroutine, and the original continuation will be
            // resumed in the same context before coro suspension. This usually happened
            // between Lazy system and other hand-crafted Awaitables.
            //  2. Awaitable has a "coAwait" method: coAwait will be called and an Awaiter
            // should returned, then co_await Awaiter will performed. Lazy<T> has coAwait
            // method, so co_await Lazy<T> will not lead to a reschedule.
            //
            // FIXME: In case awaitable is not a real awaitable, consider return
            // ReadyAwaiter instead. It would be much cheaper in case we `co_await
            // normal_function()`;
            template <typename Awaitable>
            inline auto coAwait(Executor *ex, Awaitable &&awaitable)
            {
                if constexpr (detail::HasCoAwaitMethod<Awaitable>)
                {
                    return detail::getAwaiter(std::forward<Awaitable>(awaitable).coAwait(ex));
                }
                else
                {
                    using AwaiterType = decltype(detail::getAwaiter(std::forward<Awaitable>(awaitable)));
                    return ViaAsyncAwaiter<std::decay_t<AwaiterType>>(ex, std::forward<Awaitable>(awaitable));
                }
            }
        } //
    } // namespace coro
} // namespace async_framework