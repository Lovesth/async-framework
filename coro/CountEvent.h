#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
#include <coroutine>

namespace async_framework
{
    namespace coro
    {
        namespace detail
        {
            // CountEvent is a count-down event.
            // The last 'down' will resume the awaiting coroutine on this event.
            class CountEvent
            {
            public:
                CountEvent(size_t count) : count_(count + 1) {}
                CountEvent(const CountEvent &) = delete;
                CountEvent(CountEvent &&other) : count_(other.count_.exchange(0, std::memory_order_relaxed)),
                                                 awaitingCoro_(std::exchange(other.awaitingCoro_, nullptr))
                {
                }

                [[nodiscard]] std::coroutine_handle<> down(size_t n = 1)
                {
                    // read acquire and write release, _awaitingCoro store can not be
                    // reordered after this barrier
                    auto oldCount = count_.fetch_sub(n, std::memory_order_acq_rel);
                    if (oldCount == 1)
                    {
                        auto awaitingCoro = awaitingCoro_;
                        awaitingCoro_ = nullptr;
                        return awaitingCoro;
                    }
                    else
                    {
                        return nullptr;
                        // return nullptr instead of noop_coroutine could save one time
                        // for accessing the memory.
                        // return std::noop_coroutine();
                    }
                }

                [[nodiscard]] size_t downCount(size_t n = 1)
                {
                    // read acquire and write release
                    return count_.fetch_sub(n, std::memory_order_acq_rel);
                }
                void setAwaitingCoro(std::coroutine_handle<> h) { awaitingCoro_ = h; };

            private:
                std::atomic<size_t> count_;
                std::coroutine_handle<> awaitingCoro_;
            };
        } // namespace detail
    } // namespace coro
} // namespace async_framework