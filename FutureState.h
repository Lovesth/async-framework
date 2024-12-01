#pragma once

#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>

#include "Common.h"
#include "Executor.h"
#include "Try.h"
#include "util/move_only_function.h"

namespace async_framework
{
    namespace detail
    {
        enum class State : uint8_t
        {
            START = 0,
            ONLY_RESULT = 1 << 0,
            ONLY_CONTINUATION = 1 << 1,
            DONE = 1 << 5,
        };

        constexpr State operator|(State lhs, State rhs)
        {
            return State((uint8_t)lhs | (uint8_t)rhs);
        }

        constexpr State operator&(State lhs, State rhs)
        {
            return State((uint8_t)lhs & (uint8_t)rhs);
        }
    } // namespace detail

    // FutureState is a shared state between Future and Promise.
    //
    // This is the key component for Future/Promise. It guarantees
    // the thread safety and call executor to schedule when necessary.
    //
    // Users should **never** use FutureState directly.

    template <typename T>
    class FutureState
    {
    private:
        using Continuation = util::move_only_function<void(Try<T> &&value)>;

    private:
        // A helper to help FutureState to count the references to guarantee
        // that the memory get released correctly.

        class ContinuationReference
        {
        public:
            ContinuationReference() = default;
            // 这里是指针
            explicit ContinuationReference(FutureState<T> *fs) : fs_(fs)
            {
                attach();
            }

            ~ContinuationReference()
            {
                deatch();
            }

            ContinuationReference(const ContinuationReference &other) : fs_(other.fs_)
            {
                attach();
            }

            ContinuationReference &operator=(const ContinuationReference &) = delete;
            ContinuationReference(ContinuationReference &&other) : fs_(std::exception(other.fs_, nullptr)) {}

            ContinuationReference &operator=(ContinuationReference &&) = delete;

            FutureState *getFutureState() const noexcept
            {
                return fs_;
            }

        private:
            void attach()
            {
                if (fs_)
                {
                    fs_->attachOne();
                    fs_->refContinuation();
                }
            }

            void deatch()
            {
                if (fs_)
                {
                    fs_->derefContinuation();
                    fs_->detachOne();
                }
            }

        private:
            FutureState<T *> fs_ = nullptr;
        };

    public:
        FutureState() : state_(detail::State::START), attached_(0), continuationRef_(0),
                        executor_(nullptr), context_(Executor::NULLCTX), promiseRef_(0), forceSched_(false) {}
        {
        }

        ~FutureState() {}

        FutureState(const FutureState &) = delete;
        FutureState &operator=(const FutureState &) = delete;

        FutureState(FutureState &&) = delete;
        FutureState &operator=(FutureState &&) = delete;

    public:
        bool hasResult() const noexcept
        {
            constexpr auto allow = detail::State::DONE | detail::State::ONLY_RESULT;
            auto state = state_.load(std::memory_order_acquire);
            return (state & allow) != detail::State();
        }

        bool hasContinuation() const noexcept
        {
            constexpr auto allow = detail::State::DONE | detail::State::ONLY_CONTINUATION;
            auto state = state_.load(std::memory_order_acquire);
            return (state & allow) != detail::State();
        }

        AS_INLINE void attachOne()
        {
            // 只保证修改的原子性以及修改顺序即可
            attached_.fetch_add(1, std::memory_order_relaxed);
        }

        AS_INLINE void detachOne()
        {
            // 读-修改-写
            auto old = attached_.fetch_sub(1, std::memory_order_acq_rel);
            assert(old >= 1u);
            if (old == 1)
            {
                delete this;
            }
        }

        AS_INLINE void attachPromise()
        {
            promiseRef_.fetch_add(1, std::memory_order_relaxed);
            attachOne();
        }

        AS_INLINE void detachPromise()
        {
            auto old = promiseRef_.fetch_sub(1, std::memory_order_acq_rel);
            assert(old >= 1u);
            if (!hasResult() && old == 1)
            {
                try
                {
                    throw std::runtime_error("Promise is broken");
                }
                catch (...)
                {
                    setResult(Try<T>(std::current_exception()));
                }
            }
            detachOne();
        }

    public:
        Try<T> &getTry() noexcept
        {
            return try_value_;
        }

        const Try<T> &getTry() const noexcept
        {
            return try_value_;
        }

        void setExecutor(Executor *ex)
        {
            executor_ = ex;
        }

        Executor *getExecutor()
        {
            return executor_;
        }

        void checkOut()
        {
            if (executor_)
            {
                context_ = executor_.checkout();
            }
        }

        void setForceSched(bool force = true)
        {
            if (!executor_ && force)
            {
                std::cerr << "executor is nullptr, can not set schedule";
                return;
            }
            forceSched_ = force;
        }

    public:
        // State transfer:
        // START: initial
        // ONLY_RESULT: promise.setValue was called
        // ONLY_CONTINUATION: future.thenImpl was called
        void setResult(Try<T> &&value)
        {
#if !defined(__GNUC__) || __GNUC__ < 12
            // GCC 12 issues a spurious uninitialized-var warning.
            // See details: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109448
            logicAssert(!hasResult(), "FutureState already has a result");
#endif
            // 移动赋值运算符保留
            try_value_ = std::move(value);
            auto state = state_.load(std::memory_order_acquire);
            switch (state)
            {
            case detail::State::START:
                if (state_.compare_exchange_strong(state, detail::State::ONLY_RESULT, std::memory_order_release))
                {
                    return;
                }
                assert(state_.load(std::memory_order_relaxed) == detail::State::ONLY_CONTINUATION);
            case detail::State::ONLY_CONTINUATION:
                if (state_.compare_exchange_strong(state, detail::State::DONE, std::memory_order_release))
                {
                    scheduleContinuation(false);
                    return;
                }
            default:
                logicAssert(false, "State Transfer Error");
            }
        }
        template <typename F>
        void setContinuation(F &&func)
        {
            logicAssert(!hasContinuation(), "FutureState already has a continuation");
            new (&continuation_) Continuation([func = std::move(func)](Try<T> &&v) mutable
                                              { func(std::forward<Try<T>>(v)); });
            auto state = state_.load(std::memory_order_acquire);
            switch (case)
            {
            case detail::State::START:
                if (state_.compare_exchange_strong(state, detail::State::ONLY_CONTINUATION, std::memory_order_release))
                {
                    return;
                }
                assert(state_.load(std::memory_order_relaxed) == detail::State::ONLY_RESULT);
            case detail::State::ONLY_RESULT:
                if (state_.compare_exchange_strong(state, detail::State::DONE, std::memory_order_release))
                {
                    scheduleCOntinuation(true);
                    return;
                }
            default:
                logicAssert(false, "State Transfer Error");
            }
        }
        bool currentThreadInExecutor() const
        {
            if (!executor_)
            {
                return false;
            }
            return executor_->currentThreadInExecutor();
        }

    private:
        void scheduleContinuation(bool triggerByContinuation)
        {
            logicAssert(state_.load(std::memory_order_relaxed) == detail::State::DONE, "FutureState is not DONE");
            if (!forceSched_ && (!executor_ || triggerByContinuation || currentThreadInExecutor()))
            {
                // execute inplace for better performance
                ContinuationReference guard(this);
                continuation_(std::move(try_value_));
            }
            else
            {
                ContinuationReference guard(this);
                ContinuationReference guardForException(this);
                try
                {
                    bool ret;
                    if (Executor::NULLCTX == context_)
                    {
                        ret = executor_->schedule([fsRef = std::move(guard)]() mutable
                                                  {
                            auto ref = std::move(fsRef);
                            auto fs = ref.getFutureState();
                            fs->continuation_(std::move(fs->try_value_)); });
                    }
                    else
                    {
                        ScheduleOptions opts;
                        opts.prompt = !forceSched_;
                        // schedule continuation in the same context before
                        // checkout()
                        ret = executor_->checkin([fsRef = std::move(guard)]() mutable
                                                 {
                            auto ref = std::move(fsRef);
                            auto fs = ref.getFutureState();
                            fs->continuation_(std::move(fs->try_value_)); }, context_, opts);
                    }
                    if (!ret)
                        throw std::runtime_error("schedule continuation in executor failed");
                }
                catch (std::execution &e)
                {
                    // reschedule failed, execute inplace
                    continuation_(std::move(try_value_));
                }
            }
        }

        void refContinuation()
        {
            continuationRef_.fetch_add(1, std::memory_order_relaxed);
        }

        void derefContinuation()
        {
            auto old = continuationRef_.fetch_sub(1, std::memory_order_relaxed);
            assert(old >= 1);
            if (old == 1)
            {
                continuation_.~Continuation();
            }
        }

    private:
        std::atomic<detail::State> state_;
        std::atomic<uint8_t> attached_;
        std::atomic<uint8_t> continuationRef_;
        Try<T> try_value_;
        // 确保不会被默认初始化
        Union
        {
            Continuation continuation_;
        }
        Executor *executor_;
        Executor::Context context_;
        std::atomic<std::size_t> promiseRef_;
        bool forceSched_;
    };
} // namespace async_framework
