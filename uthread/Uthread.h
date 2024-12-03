#pragma once
#include <memory>
#include "./internal/thread.h"

// 将协程模拟成用户态线程
namespace async_framework
{
    namespace uthread
    {
        struct Attribute
        {
            Executor *ex;
            size_t stack_size = 0;
        };
        // A Uthread is a stackful coroutine which would checkin/checkout based on
        // context switching. A user shouldn't use Uthread directly. He should use
        // async/await instead. See Async.h/Await.h for details.
        //
        // When a user gets a uthread returned from async. He could use `Uthread::join`
        // to set a callback for that uthread. The callback would be called when the
        // uthread finished.
        //
        // The implementation for Uthread is extracted from Boost. See
        // uthread/internal/*.S for details.

        class Uthread
        {
        public:
            Uthread() = default;
            template <typename Func>
            Uthread(Attribute attr, Func &&func) : attr_(std::move(attr))
            {
                ctx_ = std::make_unique<internal::thread_context>(std::move(func), attr_.stack_size);
            }
            ~Uthread() = default;
            Uthread(Uthread &&) = delete;
            Uthread &operator=(Uthread &&) noexcept = delete;

        public:
            template <typename Callback>
            bool join(Callback &&callback)
            {
                if (!ctx_ || ctx_->joined_)
                {
                    return false;
                }
                ctx_->joined_ = true;
                auto f = ctx_->done_.getFuture().via(attr_.ex);
                if (f.hasResult())
                {
                    callback();
                    return true;
                }
                if (!attr_.ex)
                {
                    // we can not delay the uthread life without executor.
                    // so, if the user do not hold the uthread in outside,
                    // the user can not do switch in again.
                    std::move(f).setContinuation([callback = std::move(callback)](auto &&)
                                                 { callback(); });
                }
                else
                {
                    ctx_->done_.forceSched().checkout();
                    std::move(f).setContinuation(
                        // hold on the life of uthread.
                        // user never care about the uthread's destruct.
                        [callback = std::move(callback), self = std::move(*this)](auto &&)
                        { callback(); });
                }
                return true;
            }

            void detach()
            {
                join([]() {});
            }

        private:
            Attribute attr_;
            std::unique_ptr<internal::thread_context> ctx_;
        };
    } // namespace uthread
} // namesapce async_framework