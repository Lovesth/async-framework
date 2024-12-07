#pragma once

#include <mutex>
#include "./Lazy.h"

namespace async_framework
{
    namespace coro
    {
        template <typename Lock>
        class ConditionVariableAwaiter;

        template <typename Lock>
        class ConditionVariable
        {
        public:
            ConditionVariable() noexcept {}
            ~ConditionVariable() {}

            ConditionVariable(const ConditionVariable &) = delete;
            ConditionVariable &operator=(const ConditionVariable &) = delete;

            void notify() noexcept { notifyAll(); }
            void notifyOne() noexcept;
            void notifyAll() noexcept;

            template <typename Pred>
            Lazy<> wait(Lock &lock, Pred &&pred) noexcept;

        private:
            void resumeWaiters(ConditionVariableAwaiter<Lock> *awaiters);

        private:
            friend class ConditionVariableAwaiter<Lock>;
            std::atomic<ConditionVariableAwaiter<Lock> *> awaiters_ = nullptr;
        };

        template <typename Lock>
        class ConditionVariableAwaiter
        {
        public:
            ConditionVariableAwaiter(ConditionVariable<Lock> *cv, Lock &lock) noexcept : cv_(cv), lock_(lock)
            {
            }

            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;
                std::unique_lock<Lock> lock{lock_, std::adopt_lock};
                auto awaitings = cv_->awaiters_.load(std::memory_order_relaxed);
                do
                {
                    next_ = awaitings;
                } while (!cv_->awaiters_.compare_exchange_weak(awaitings, this, std::memory_order_acquire, std::memory_order_relaxed));
            }

            void await_resume() const noexcept {}

        public:
            ConditionVariable<Lock> *cv_;
            Lock &lock_;

        private:
            friend class ConditionVariable<Lock>;
            ConditionVariableAwaiter<Lock> *next_ = nullptr;
            std::coroutine_handle<> continuation_;
        };

        template <typename Lock>
        template <typename Pred>
        inline Lazy<> ConditionVariable<Lock>::wait(Lock &lock, Pred &&pred) noexcept
        {
            while (!pred())
            {
                co_await ConditionVariableAwaiter<Lock>{this, lock};
                co_await lock.coLock();
            }
            co_return;
        }
    } // namespace coro
} // namespace async_framework