#pragma once

#include <cstddef>
#include <limits>
#include "./ConditionVariable.h"
#include "./SpinLock.h"

namespace async_framework::coro
{
    // Analogous to `std::counting_semaphore`, but it's for `Lazy`.
    // A counting_semaphore contains an internal counter initialized by the
    // constructor. This counter is decremented by calls to acquire() and related
    // methods, and is incremented by calls to release(). When the counter is zero,
    // acquire() blocks until the counter is incremented, It will suspend the
    // current coroutine and switch to other coroutines to run. But try_acquire()
    // does not block;

    template <std::size_t LeastMaxValue = std::numeric_limits<std::uint32_t>::max()>
    class CountingSemaphore
    {
    public:
        static_assert(LeastMaxValue <= std::numeric_limits<std::uint32_t>::max());
        explicit CountingSemaphore(std::size_t desired) : count_(desired) {}
        ~CountingSemaphore() = default;
        CountingSemaphore(const CountingSemaphore &) = delete;
        CountingSemaphore &operator=(const CountingSemaphore &) = delete;

        // returns the maximum possible value of the internal counter
        static constexpr size_t max() noexcept { return LeastMaxValue; }

        // decrease the internal counter or blocks until it can
        // If the internal counter is 0, the current coroutine will be suspended
        Lazy<void> acquire() noexcept;
        // Increase the internal counter and unblocks acquires
        Lazy<void> release(std::size_t update = 1) noexcept;

        // tries to decrease the internal counter without blocking
        Lazy<bool> try_acquire() noexcept;

        // TODO: To implement
        // TODO: To implement
        // template <class Rep, class Period>
        // bool try_acquire_for(std::chrono::duration<Rep, Period> expires_in);
        // template <class Clock, class Duration>
        // bool try_acquire_until(std::chrono::time_point<Clock, Duration>
        // expires_at);

    private:
        using MutexType = SpinLock;
        MutexType lock_;
        ConditionVariable<MutexType> cv_;
        std::size_t count_;
    };

    using BinarySemaphore = CountingSemaphore<1>;
    template <std::size_t LeastMaxValue>
    Lazy<void> CountingSemaphore<LeastMaxValue>::acquire() noexcept
    {
        auto lock = co_await lock_.coScopedLock();
        co_await cv_.wait(lock_, [this]
                          { return count_ > 0; });
        --count_;
    }

    template <std::size_t LeastMaxValue>
    Lazy<void> CountingSemaphore<LeastMaxValue>::release(std::size_t update) noexcept
    {
        // update should be less than LeastMaxValue and greater than 0
        assert(update <= LeastMaxValue && update != 0);
        auto lock = co_await lock_.coScopedLock();
        // internal counter should be less than LeastMaxValue
        assert(count_ <= LeastMaxValue - update);
        count_ += update;
        if (update > 1)
        {
            // When update is greater than 1, wake up all coroutines.
            // When the counter is decremented to 0, resuspend the coroutine.
            cv_.notifyAll();
        }
        else
        {
            cv_.notifyOne();
        }
    }

    template <std::size_t LeastMaxValue>
    Lazy<bool> CountingSemaphore<LeastMaxValue>::try_acquire() noexcept
    {
        auto lock = co_await lock_.coScopedLock();
        if (count_)
        {
            --count_;
            co_return true;
        }
        co_return false;
    }
} // namespace async_framework::coro
