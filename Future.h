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
        Future(FutureState<inner_value_type> *fs) : sharedState_(fs)
        {
            if (sharedState_)
            {
                sharedState_->attachOne();
            }
        }

        Future(Try<inner_value_type> &&t) : sharedState_(nullptr), localState(std::move(t)) {}

        ~Future()
        {
            if (sharedState_)
            {
                sharedState_->detachOne();
            }
        }

        Future(const Future &) = delete;
        Future &operator=(const Future &) = delete;

        Future(Future &&other) : sharedState_(other.sharedState_), localState_(std::move(other.localState_))
        {
            other.sharedState_ = nullptr;
        }

        Future &operator(Future &&other)
        {
            if (this != &other)
            {
                std::swap(sharedState_, other.sharedState_);
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
            return sharedState_ != nullptr || localState_.hasResult();
        }

        bool hasResult() const
        {
            return localState_.hasResult() || sharedState_->hasResult();
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

        Try<T> &&result() &&
            requires(!std::is_void_v<T>)
        {
            return std::move(getTry(*this));
        }

        Try<T> &result() &
            requires(!std::is_void_v<T>)
        {
            return getTry(*this);
        }

        const Try<T> &result() const &
            requires(!std::is_void_v<T>)
        {
            return getTry(*this);
        }

        Try<void> result() &&
            requires(std::is_void_v<T>)
        {
            return getTry(*this);
        }

        Try<void> result() &
            requires(std::is_void_v<T>)
        {
            return getTry(*this);
        }

        Try<void> result() const &
            requires(std::is_void_v<T>)
        {
            return getTry(*this);
        }

        // get is only allowed on rvalue, aka, Future is not valid after get
        // invoked.
        //
        // Get value blocked thread when the future doesn't have a value.
        // If future in uthread context, use await(future) to get value without
        // thread blocked.

        T get() &&
        {
            wait();
            return (std::move(*this)).value();
        }

        // Implemention for get() to wait synchronously.
        void wait()
        {
            logicAssert(valid(), "Future is broken.");
            if (hasResult())
            {
                return;
            }

            // wait in the same executor may cause deadlock.
            assert(!currentThreadInExecutor());

            // the state is a shared state
            Promise<T> promise;
            auto future = promise.getFuture();

            // following continuation is simpl, execute inplace
            sharedState_->setExecutor(nullptr);

            std::mutex mtx;
            std::condition_variable cv;
            std::atomic<bool> done{false};

            sharedState_->setContinuation([&mtx, &cv, &done, p = std::move(promise)](Try<T> &t) mutable
                                          {
                std::unique_lock<std::mutex> lock(mtx);
                p.setValue(std::move(t));
                done.store(true, std::memory_order_relaxed);
                cv.notify_one(); });

            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&done]()
                    { return done.load(std::memory_order_relaxed); });
            *this = std::move(future);
            assert(sharedState_->hasResult());
        }

        // Set the executor for the future. This only works for rvalue.
        // So the original future shouldn't be accessed after setting
        // an executor. The user should use the returned future instead.
        Future<T> via(Executor *executor) &&
        {
            setExecutor(executor);
            Future<T> ret(std::move(*this));
            // 注意via函数只能用于右值，因此需要返回一个新的Future<T>
            return ret;
        }

        // thenTry() is only allowed on rvalues, do not access a future after
        // thenTry() called. F is a callback function which takes Try<T>&& as
        // parameter.
        //
        template <typename F, typename R = TryCallableResult<T, F>>


    public:
        // This section is public because they may invoked by other type of Future.
        // They are not suppose to be public.
        // FIXME: mark the section as private.
        void setExecutor(Executor *ex)
        {
            if (sharedState_)
            {
                sharedState_->setExecutor(ex);
            }
            else
            {
                localState_.setExecutor(ex);
            }
        }

        Executor *getExecutor()
        {
            if (sharedState_)
            {
                return sharedState_->getExecutor();
            }
            else
            {
                return localState_.getExecutor();
            }
        }

        template <typename F>
        void setContinuation(F &&func)
        {
            assert(valid());
            if (sharedState_)
            {
                sharedState_->setContinuation(std::forward<F>(func));
            }
            else
            {
                localState_.setContinuation(std::forward<F>(func));
            }
        }

        bool currentThreadInExecutor() const
        {
            assert(valid());
            if (sharedState_)
            {
                // caller in executor?
                return sharedState_->currentThreadInExecutor();
            }
            else
            {
                return localState_.currentThreadInExecutor();
            }
        }

        bool TEST_hasLocalState() const
        {
            return localState_.hasResult();
        }

    private:
        template <typename T>
        static decltype(auto) getTry(T &self)
        {
            logicAssert(self.valid(), "Future is broken.");
            logicAssert(self.localState_.hasResult() || self.sharedState_.hasResult(), "Future is not ready");
            if (self.sharedState_)
            {
                return self.sharedState_->getTry();
            }
            else
            {
                return self.localState_.getTry();
            }
        }

        // continuation returns a future
        template <typename F, typename R>
        Future<typename R::Return>

    private:
        FutureState<inner_value_type> *sharedState_;
        // Ready-Future does not have a Promise, an inline state is faster.
        LocalState<inner_value_type> localState_;

    private:
        template <Iter>
        friend Future<std::vector<Try<typename std::iterator_traits<Iter>::value_type::value_type>>>
        collectAll(Iter begin, Iter end);
    };
}