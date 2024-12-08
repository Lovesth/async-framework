#pragma once

#include "../Common.h"
#include "../Executor.h"
#include "../Try.h"
#include "../util/Condition.h"

namespace async_framework
{
    namespace coro
    {
        // Sync await on a coroutine, block until coro finished, coroutine result will be returned.
        // Do not syncAwait in the same executor with Lazy, this may lead to a deadlock.
        template <typename LazyType>
        inline auto syncAwait(LazyType &&lazy)
        {
            auto executor = lazy.getExecutor();
            if (executor)
            {
                logicAssert(!executor->currentThreadInExecutor(), "do not sync await in the same executor with Lazy");
            }
            util::Condition cond;
            using ValueType = typename std::decay_t<LazyType>::ValueType;

            Try<ValueType> value;
            std::move(std::forward<LazyType>(lazy)).start([&cond, &value](Try<ValueType> result)
                                                          {
                value = std::move(result);
                cond.release(); });
            cond.acquire();
            return std::move(value).value();
        }

        // A simple wrapper to ease the use.
        template <typename LazyType>
        inline auto syncAwait(LazyType &&lazy, Executor *ex)
        {
            return syncAwait(std::move(std::forward<LazyType>(lazy).via(ex)));
        }

    } // namespace coro
} // namespace async_framework