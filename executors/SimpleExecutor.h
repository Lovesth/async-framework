#pragma once

#include <functional>
#include "../Executor.h"
#include "SimpleIOExecutor.h"
#include "../thread_pool/ThreadPool.h"

namespace async_framework
{
    namespace executors
    {
        inline constexpr int64_t kContextMask = 0x40000000;

        // This is a simple executor. The intention of SimpleExecutor is to make the
        // test available and show how user should implement their executors. People who
        // want to have fun with async_simple could use SimpleExecutor for convenience,
        // too. People who want to use async_simple in production level development
        // should implement their own executor strategy and implement an Executor
        // derived from async_simple::Executor as an interface.
        //
        // The actual strategy that SimpleExecutor used is implemented in
        // async_simple/util/ThreadPool.h.

        // 这里的Schedule是使用线程池实现
        class SimpleExecutor : public Executor
        {
        public:
            using Func = Executor::Func;
            using Context = Executor::Context;

        public:
            explicit SimpleExecutor(size_t threadNum) : pool_(threadNum)
            {
                ioExecutor_.init();
            }

            ~SimpleExecutor()
            {
                ioExecutor_.destroy();
            }

        public:
            bool schedule(Func func) override
            {
                return pool_.scheduleById(std::move(func)) == util::ThreadPool::ERROR_TYPE::ERROR_NONE;
            }

            bool currentThreadInExecutor() const override
            {
                return pool_.getCurrentId() != -1;
            }

            ExecutorStat stat() const override
            {
                return ExecutorStat();
            }

            size_t currentContextId() const override
            {
                return pool_.getCurrentId();
            }

            Context checkout() override
            {
                // avoid CurrentId equal to NULLCTX
                return reinterpret_cast<Context>(pool_.getCurrentId() | kContextMask);
            }

            bool checkin(Func func, Context ctx, ScheduleOptions opts) override
            {
                int64_t id = reinterpret_cast<int64_t>(ctx);
                auto prompt = pool_.getCurrentId() == (id & (~kContextMask)) && opts.prompt;
                if (prompt)
                {
                    func();
                    return true;
                }
                return pool_.scheduleById(std::move(func), id & (~kContextMask)) == util::ThreadPool::ERROR_TYPE::ERROR_NONE;
            }

            IOExecutor *getIOExecutor() override
            {
                return &ioExecutor_;
            }

        private:
            util::ThreadPool pool_;
            SimpleIOExecutor ioExecutor_;
        };
    } // namespace executors
} // namespace async_framework