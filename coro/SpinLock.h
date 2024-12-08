#pragma once
#include <mutex>
#include <thread>
#include "./Lazy.h"

namespace async_framework
{
    namespace coro
    {
        class SpinLock
        {
        public:
            explicit SpinLock(std::int32_t count = 1024) noexcept
                : spinCount_(count), locked_(false)
            {
            }

            bool tryLock() noexcept
            {
                return !locked_.exchange(true, std::memory_order_acquire);
            }

            Lazy<> coLock() noexcept
            {
                auto counter = spinCount_;
                while (!tryLock())
                {
                    while (locked_.load(std::memory_order_relaxed))
                    {
                        if (counter-- <= 0)
                        {
                            // try spinCount_ times and then go to sleep.
                            co_await Yield{};
                            counter = spinCount_;
                        }
                    }
                }
                co_return;
            }

            void lock() noexcept
            {
                auto counter = spinCount_;
                while (!tryLock())
                {
                    while (locked_.load(std::memory_order_relaxed))
                    {
                        if (counter-- <= 0)
                        {
                            std::this_thread::yield();
                            counter = spinCount_;
                        }
                    }
                }
            }

            void unlock() noexcept
            {
                locked_.store(false, std::memory_order_release);
            }

            Lazy<std::unique_lock<SpinLock>> coScopedLock() noexcept
            {
                co_await coLock();
                co_return std::unique_lock<SpinLock>{*this, std::adopt_lock};
            }

        private:
            std::int32_t spinCount_;
            std::atomic<bool> locked_;
        };

        class ScopedSpinLock
        {
        public:
            explicit ScopedSpinLock(SpinLock &lock) : lock_(lock) { lock_.lock(); }
            ~ScopedSpinLock()
            {
                lock_.unlock();
            }

        private:
            ScopedSpinLock(const ScopedSpinLock &) = delete;
            ScopedSpinLock &operator=(const ScopedSpinLock &) = delete;
            SpinLock &lock_;
        };
    } // namespace coro
} // namespace async_framework