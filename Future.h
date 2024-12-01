#pragma once

#include <condition_variable>
#include <mutex>
#include <type_traits>
#include "Executor.h"
#include "FutureState.h"
#include "LocalState.h"
#include "Traits.h"

namespace async_framework
{
    template <typename T>
    class Promise;

    // The well-known Future/Promise pairs mimic a producer/consumer pair.
    // The Future stands for the consumer-side.
    //
    // Future's implementation is thread-safe so that Future and Promise
    // could be able to appear in different thread.
    //
    // To get the value of Future synchronously, user should use `get()`
    // method. It would blocking the current thread by using condition variable.
    //
    // To get the value of Future asynchronously, user could use `thenValue(F)`
    // or `thenTry(F)`. See the separate comments for details.
    //
    // User shouldn't access Future after Future called `get()`, `thenValue(F)`,
    // or `thenTry(F)`.
    //
    // User should get a Future by calling `Promise::getFuture()` instead of
    // constructing a Future directly. If the user want a ready future indeed,
    // he should call makeReadyFuture().

    template <typename T>
    class Future
    {
    private:
        // If T is void, the inner_value_type is Unit. It will be used by
        // `FutureState` and `LocalState`. Because `Try<void>` cannot distinguish
        // between `Nothing` state and `Value` state.
        // It maybe remove Unit after next version, and then will change the
        // `Try<void>` to distinguish between `Nothing` state and `Value` state

        using inner_value_type = std::conditional_t<std::is_void_v<T>, Unit, T>;

    public:
        using value_type = T;
        Future(FutureState<inner_value_type> *fs) : shareState_(fs)
        {
            if (shareState_)
            {
                shareState_->attachOne();
            }
        }

        Future(Try<inner_value_type> &&t) : shareState_(nullptr), localState(std::move(t)) {}

        ~Future()
        {
            if (shareState_)
            {
                shareState_->detachOne();
            }
        }

        Future(const Future &) = delete;
        Future &operator=(const Future &) = delete;

        Future(Future &&other) : shareState_(other.shareState_), localState_(std::move(other.localState_))
        {
            other.shareState_ = nullptr;
        }

        Future &operator(Future &&other)
        {
            if (this != &other)
            {
                std::swap(shareState_, other.shareState_);
                localState_ = std::move(other.localState_);
            }
            return *this;
        }

        auto coAwait(Executor *) && noexcept
        {
            return std::move(*this);
        }

    public:
        bool valid() const
        {
            return shareState_ != nullptr || localState_.hasResult();
        }

        bool hasResult() const
        {
            return localState_.hasResult() || shareState_->hasResult();
        }

        std::add_rvalue_reference_t<T> value() &&
        {
            if constexpr (std::is_void_v<T>)
            {
                return result().value();
            }
            else
            {
                return std::move(result().value());
            }
        }

        std::add_lvalue_reference_t<T> value() &
        {
            return result().value();
        }

        const std::add_lvalue_reference_t<T> value() const &
        {
            return result().value();
        }

        // Try<T>&& result() && requires(!std::is_void_v<T>) {
        //     return std::move()
        // }
    private:
        template <typename T>
        static decltype(auto) getTry(T& self) {
            logicAssert(self.valid(), "Future is broken.");
        }
    private:
        FutureState<inner_value_type> *shareState_;
        // Ready-Future does not have a Promise, an inline state is faster.
        LocalState<inner_value_type> localState_;

    private:
        template <Iter>
        friend Future<std::vector<Try<typename std::iterator_traits<Iter>::value_type::value_type>>>
        collectAll(Iter begin, Iter end);
    };
}