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

        private:
            std::int32_t spinCount_;
            std::atomic<bool> locked_;
        };
    } // namespace coro
} // namespace async_framework