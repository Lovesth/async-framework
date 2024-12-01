#pragma once

#include <functional>
#include <utility>
#include "Executor.h"
#include "Try.h"

namespace async_framework
{
    // A component of Future/Promise. LocalState is owned by
    // Future only. LocalState should be valid after
    // Future and Promise disconnects.
    //
    // Users should never use LocalState directly.

    template <typename T>
    class LocalState
    {
    private:
        using Continuation = std::function<void(Try<T> &&value)>;

    public:
        LocalState() : executor_(nullptr) {}
        LocalState(T &&v) try_value_(std::forward<T>(v)), executor_(nullptr) {}
        LocalState(Try<T> &&t) : try_value_(std::move(t)), executor_(nullptr) {}

        ~LocalState() {}

        LocalState(const LocalState &) = delete;
        LocalState &operator=(const LocalState &) = delete;

        LocalState(LocalState &&other) : try_value_(std::move(other.try_value_)), executor_(std::exchange(other.executor_, nullptr)) {}
        LocalState &operator=(LocalState &&other)
        {
            if (this != &other)
            {
                std::swap(try_value_, other.try_value_);
                std::swap(executor_, other.executor_);
            }
            return *this;
        }

    public:
        bool hasResult() const noexcept
        {
            return try_value_.available();
        }

    public:
        Try<T> &getTry() noexcept { return try_value_; }
        const Try<T> &getTry() const noexcept { return try_value_; }

        void setExecutor(Executor *ex)
        {
            executor_ = ex;
        }

        Executor *getExecutor()
        {
            return executor_;
        }

        bool currentThreadInExecutor() const
        {
            if (!executor_)
            {
                return false;
            }
            return executor_->currentThreadInExecutor();
        }

        template <typename F>
        void setContinuation(F &&f)
        {
            assert(try_value_.available());
            std::forward<F>(f)(std::move(try_value_));
        }

    private:
        Try<T> try_value_;
        Executor *executor_;
    };
} // namespace async_framework
