#pragma once

#include <thread>
#include <cstdint>
#include "../Executor.h"
#include "./Lazy.h"

namespace async_framework
{
    namespace coro
    {
        // Returns an awaitable that would return after dur times.
        //
        // e.g. co_await sleep(100s);
        template <typename Rep, typename Period>
        Lazy<void> sleep(std::chrono::duration<Rep, Period> dur, uint64_t schedule_hint)
        {
            auto ex = co_await CurrentExecutor();
            if (!ex)
            {
                std::this_thread::sleep_for(dur);
                co_return;
            }
            co_return co_await ex->after(std::chrono::duration_cast<Executor::Duration>(dur), schedule_hint);
        }

        template <typename Rep, typename Period>
        Lazy<void> sleep(std::chrono::duration<Rep, Period> dur)
        {
            return sleep(dur, static_cast<uint64_t>(async_framework::Executor::Priority::DEFAULT));
        }

        template <typename Rep, typename Period>
        Lazy<void> sleep(Executor *ex, std::chrono::duration<Rep, Period> dur, uint64_t schedule_info)
        {
            co_return co_await ex->after(std::chrono::duration_cast<Executor::Duration>(dur), schedule_info);
        }

        template <typename Rep, typename Period>
        Lazy<void> sleep(Executor *ex, std::chrono::duration<Rep, Period> dur)
        {
            return sleep(ex, dur, static_cast<uint64_t>(async_framework::Executor::Priority::DEFAULT));
        }
    } // namespace coro
} // namespace async_framework