#pragma once
#include <atomic>
#include <mutex>
#include <coroutine>
#include <cassert>

namespace async_framework
{
    namespace coro
    {
        class Mutex
        {
        private:
            class ScopedLockAwaiter;
            class LockAwaiter;

        public:
            // Construct a new async mutex that is initially unlocked.
            Mutex() noexcept : state_(unlockedState()), waiters_(nullptr) {}

            Mutex(const Mutex &) = delete;
            Mutex(Mutex &&) = delete;
            Mutex &operator=(const Mutex &) = delete;
            Mutex &operator=(Mutex &&) = delete;

            ~Mutex()
            {
                assert(state_.load(std::memory_order_relaxed) == unlockedState() || state_.load(std::memory_order_relaxed) == nullptr);
                assert(waiters_ == nullptr);
            }

            /// Try to lock the mutex synchronously.
            ///
            /// Returns true if the lock was able to be acquired synchronously, false
            /// if the lock could not be acquired because it was already locked.
            ///
            /// If this method returns true then the caller is responsible for ensuring
            /// that unlock() is called to release the lock.
            bool tryLock() noexcept
            {
                void *oldValue = unlockedState();
                return state_.compare_exchange_strong(oldValue, nullptr, std::memory_order_acquire, std::memory_order_relaxed);
            }

            /// Lock the mutex asynchronously, returning an RAII object that will
            /// release the lock at the end of the scope.
            ///
            /// You must co_await the return value to wait until the lock is acquired.
            ///
            /// Chain a call to .via() to specify the executor to resume on when
            /// the lock is eventually acquired in the case that the lock could not be
            /// acquired synchronously. Note that the executor will be passed implicitly
            /// if awaiting from a Task or AsyncGenerator coroutine. The awaiting
            /// coroutine will continue without suspending if the lock could be acquired
            /// synchronously.
            [[nodiscard]] ScopedLockAwaiter coScopedLock() noexcept;

            /// Lock the mutex asynchronously.
            ///
            /// You must co_await the return value to wait until the lock is acquired.
            ///
            /// Chain a call to .via() to specify the executor to resume on when
            /// the lock is eventually acquired in the case that the lock could not be
            /// acquired synchronously. The awaiting coroutine will continue without
            /// suspending if the lock could be acquired synchronously.
            ///
            /// Once the 'co_await m.coLock()' operation completes, the awaiting
            /// coroutine is responsible for ensuring that .unlock() is called to
            /// release the lock.
            ///
            /// Consider using coScopedLock() instead to obtain a std::scoped_lock
            /// that handles releasing the lock at the end of the scope.
            [[nodiscard]] LockAwaiter coLock() noexcept;

            /// Unlock the mutex.
            ///
            /// If there are other coroutines waiting to lock the mutex then this will
            /// schedule the resumption of the next coroutine in the queue.

        private:
            class LockAwaiter
            {
            public:
                explicit LockAwaiter(Mutex &mutex) noexcept : mutex_(mutex) {}

                bool await_ready() noexcept { return mutex_.tryLock(); }

                bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
                {
                    awaitingCoroutine_ = awaitingCoroutine;
                    return mutex_.lockAsyncImpl(this);
                }

                void await_resume() noexcept {}

            protected:
                Mutex &mutex_;

            private:
                friend Mutex;
                std::coroutine_handle<> awaitingCoroutine_;
                LockAwaiter *next_;
            };

            class ScopedLockAwaiter : public LockAwaiter
            {
                using LockAwaiter::LockAwaiter;
                [[nodiscard]] std::unique_lock<Mutex> await_resume() noexcept
                {
                    return std::unique_lock<Mutex>{mutex_, std::adopt_lock};
                }
            };

            // Special value for state_ that indicates the mutex is not locked.
            void *
            unlockedState() noexcept
            {
                return this;
            }

            // Try to lock the mutex.
            //
            // Returns true if the lock could not be acquired synchronously and awaiting
            // coroutine should suspend. In this case the coroutine will be resumed
            // later once it acquires the mutex. Returns false if the lock was acquired
            // synchronously and the awaiting coroutine should continue without
            // suspending.
            bool lockAsyncImpl(LockAwaiter *awaiter)
            {
                void *oldValue = state_.load(std::memory_order_relaxed);
                while (true)
                {
                    if (oldValue == unlockedState())
                    {
                        void *newValue = nullptr;
                        if (state_.compare_exchange_weak(oldValue, newValue, std::memory_order_acquire, std::memory_order_relaxed))
                        {
                            // Acquired synchronously, don't suspend
                            return false;
                        }
                    }
                    else
                    {
                        // It looks like the mutex is currently locked.
                        // Try to queue this waiter to the list of waiters.
                        void *newValue = awaiter;
                        awaiter->next_ = static_cast<LockAwaiter *>(oldValue);
                        if (state_.compare_exchange_weak(oldValue, newValue, std::memory_order_relaxed, std::memory_order_relaxed))
                        {
                            return true;
                        }
                    }
                }
            }

            // This contains either:
            // - this    => Not locked
            // - nullptr => Locked, no newly queued waiters (ie. empty list of waiters)
            // - other   => Pointer to first LockAwaiter* in a linked-list of newly
            //              queued awaiters in LIFO order.
            std::atomic<void *> state_;

            // Linked-list of waiters in FIFO order.
            // Only the current lock holder is allowed to access this member.
            LockAwaiter *waiters_;
        };

    } // namespace coro
} // async_framework