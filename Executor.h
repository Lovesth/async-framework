#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <ratio>
#include <string>
#include <thread>
#include <coroutine>
#include "MoveWrapper.h"
#include "move_only_function.h"

namespace async_framework
{
    // executor的状态信息，保存了executor中等待执行的任务数
    struct ExecutorStat
    {
        size_t pendingTaskCount = 0;
        ExecutorStat() = default;
    };

    // Options for a schedule.
    // The option contains:
    // - bool prompt. Whether or not this schedule
    //   should be prompted.
    struct ScheduleOptions
    {
        bool prompt = true;
        ScheduleOptions() = default;
    };

    // Awaitable to get the current executor.
    // For example:
    // ```
    //  auto current_executor =
    //      co_await CurrentExecutor{};
    // ```
    struct CurrentExecutor
    {
    };

    // Executor is a scheduler for functions.
    //
    // Executor is a key component for scheduling coroutines.
    // Considering that there should be already an executor
    // in most production-level programs, Executor is designed
    // to be able to fit the scheduling strategy in existing programs.
    //
    // User should derive from Executor and implement their scheduling
    // strategy.
    class IOExecutor;

    class Executor
    {
    public:
        // Context is an identification for the context where an executor
        // should run. See checkin/checkout for details.
        using Context = void *;
        static constexpr Context NULLCTX = nullptr;

        // A time duration in microseconds
        using Duration = std::chrono::duration<int64_t, std::micro>;

        // The schedulable function. Func should accept no argument and
        // return void.
        using Func = std::function<void()>;
        class TimeAwaitable;
        class TimeAwaiter;

        Executor(std::string name = "default") : name_(name)
        {
        }

        virtual ~Executor()
        {
        }

        Executor(const Executor &) = delete;
        Executor &operator=(const Executor &) = delete;

        // Schedule a function.
        // `schedule` would return false if schedule failed, which means function
        // func will not be executed. In case schedule return true, the executor
        // should guarantee that the func would be executed.

        virtual bool schedule(Func func) = 0;

        // 4-bits priority, less level is more important. Default
        // value of async-simple schedule is DEFAULT. For scheduling level >=
        // YIELD, if executor always execute the work immediately if other
        // works, it may cause dead lock. are waiting.

        enum class Priority
        {
            HIGHEST = 0x0,
            DEFAULT = 0x7,
            YIELD = 0x8,
            LOWEST = 0xF,
        };

        // Low 16-bit of schedule_info is reserved for async-simple, and the lowest
        // 4-bit is stand for priority level. The implementation of scheduling logic
        // isn't necessary, which is determined by implementation. However, to avoid
        // spinlock/yield deadlock, when priority level >= YIELD, scheduler
        // can't always execute the work immediately when other works are
        // waiting.

        virtual bool schedule(Func func, uint64_t schedule_info)
        {
            return schedule(std::move(func));
        }

        bool schedule_move_only(util::move_only_function<void()> func)
        {
            MoveWrapper<decltype(func)> tmp(std::move(func));
            return schedule([func = tmp]()
                            { func.get()(); });
        }

        bool schedule_move_only(util::move_only_function<void()> func, uint64_t schedule_info)
        {
            MoveWrapper<decltype(func)> tmp(std::move(func));
            return schedule([func = tmp]()
                            { func.get()(); });
        }

        // Return true if caller runs in the executor.
        virtual bool currentThreadInExecutor() const
        {
            throw std::logic_error("Not implemented");
        }

        virtual ExecutorStat stat() const
        {
            throw std::logic_error("Not implemented");
        }

        // checkout() return current "Context", which defined by executor implementation,
        // then checkin(func, "Context") should schedule func to the same Context as before.
        virtual size_t currentContextId() const { return 0; }
        virtual Context checkout() { return NULLCTX; }

        virtual bool checkin(Func func, [[maybe_unused]] Context ctx, [[maybe_unused]] ScheduleOptions opts)
        {
            return schedule(std::move(func));
        }

        virtual bool checkin(Func func, Context ctx)
        {
            static ScheduleOptions opts;
            return checkin(std::move(func), ctx, opts);
        }

        const std::string &name() const
        {
            return name_;
        }

        // Use co_await executor.after(sometime)
        // to schedule current execution after some time
        TimeAwaitable after(Duration dur);

        // Use to co_await ececutor.after(sometime)
        // to schedule current execution after some time
        TimeAwaitable after(Duration dur, uint64_t schedule_info);

        // IOExecutor accepts IO read/after requests.
        // Return nullptr if the executor doesn't offer an IOExecutor.
        virtual IOExecutor *getIOExecutor()
        {
            throw std::logic_error("Not implemented");
        }

    protected:
        virtual void schedule(Func func, Duration dur)
        {
            std::thread([this, func = std::move(func), dur]()
                        {
                std::this_thread::sleep_for(dur);
                schedule(std::move(func)); })
                .detach();
        }

        virtual void schedule(Func func, Duration dur, uint64_t schedule_info)
        {
            schedule(std::move(func), dur);
        }

    private:
        std::string name_;
    };

    // Awaiter to implement Executor::after
    class Executor::TimeAwaiter
    {
    public:
        TimeAwaiter(Executor *ex, Executor::Duration dur, uint64_t schedule_info) : ex_(ex), dur_(dur), schedule_info_(schedule_info)
        {
        }

    public:
        bool await_ready() const noexcept
        {
            return false;
        }

        template <typename PromiseType>
        void await_suspend(std::coroutine_handle<PromiseType> continuation)
        {
            ex_->schedule(std::move(continuation), dur_, schedule_info_);
        }
        void resume() const noexcept {}

    private:
        Executor *ex_;
        Executor::Duration dur_;
        uint64_t schedule_info_;
    };

    // Awaitable to implement Executor::after.
    class Executor::TimeAwaitable
    {
    public:
        TimeAwaitable(Executor *ex, Executor::Duration dur, uint64_t schedule_info) : ex_(ex), dur_(dur), schedule_info_(schedule_info) {}
        auto coAwait()
        {
            return Executor::TimeAwaiter(ex_, dur_, schedule_info_);
        }

    private:
        Executor *ex_;
        Executor::Duration dur_;
        uint64_t schedule_info_;
    };

    Executor::TimeAwaitable inline Executor::after(Executor::Duration dur)
    {
        return Executor::TimeAwaitable(this, dur, static_cast<uint64_t>(Executor::Priority::DEFAULT));
    }

    Executor::TimeAwaitable inline Executor::after(Executor::Duration dur, uint64_t schedule_info)
    {
        return Executor::TimeAwaitable(this, dur, schedule_info);
    }
} // namespace async_frame work.