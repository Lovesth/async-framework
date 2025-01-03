#pragma once

#include <cstddef>
#include <cstdio>
#include <execution>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include "../Common.h"
#include "../Try.h"
#include "./DetachedCoroutine.h"
#include "./LazyLocalBase.h"
#include "./PromiseAllocator.h"
#include "./ViaCoroutine.h"
#include <coroutine>

namespace async_framework
{
    class Executor;
    namespace coro
    {
        template <typename T>
        class Lazy;

        // In the middle of the execution of one coroutine, if we want to give out the
        // rights to execute back to the executor, to make it schedule other tasks to
        // execute, we could write:
        //
        // ```C++
        //  co_await Yield{};
        // ```
        //
        // This would suspend the executing coroutine.

        struct Yield
        {
        };

        template <typename T = LazyLocalBase>
        struct CurrentLazyLocal
        {
        };

        namespace detail
        {
            template <typename, typename OAlloc, bool Para>
            struct CollectAllAwaiter;

            template <bool Para, template <typename> typename LazyType, typename... Ts>
            struct CollectAllVariadicAwaiter;

            template <typename LazyType, typename IAlloc, typename Callback>
            struct CollectAnyAwaiter;

            template <template <typename> typename LazyType, typename... Ts>
            struct CollectAnyVariadicAwaiter;

            template <typename... Ts>
            struct CollectAnyVariadicPairAwaiter;

        } // namespace detail

        namespace detail
        {
            class LazyPromiseBase : public PromiseAllocator<void, true>
            {
            public:
                // Resume the caller waiting to the current coroutine. Note that we need
                // destroy the frame for the current coroutine explicitly. Since after
                // FinalAwaiter, The current coroutine should be suspended and never to
                // resume. So that we couldn't expect it to release it self any more.
                struct FinalAwaiter
                {
                    bool await_ready() const noexcept { return false; }
                    template <typename PromiseType>
                    auto await_suspend(std::coroutine_handle<PromiseType> h) noexcept
                    {
                        static_assert(std::is_base_of_v<LazyPromiseBase, PromiseType>, "the final awaiter is only allowed to be called by Lazy");
                        return h.promise().continuation_;
                    }
                    void await_resume() noexcept {}
                };

                struct YieldAwaiter
                {
                    YieldAwaiter(Executor *executor) : executor_(executor) {}
                    bool await_ready() const noexcept
                    {
                        return false;
                    }

                    // handle重载了operator(), handle.operator()() = handle.resume
                    template <typename PromiseType>
                    void await_suspend(std::coroutine_handle<PromiseType> handle)
                    {
                        static_assert(std::is_base_of_v<LazyPromiseBase, PromiseType>, "co_await Yield is only allowed to be called by Lazy");
                        logicAssert(executor_, "Yielding is only meaningful with an executor!");

                        // schedule_info is YIELD here, which avoid executor always
                        // run handle immediately when other works are waiting, which may
                        // cause deadlock.
                        executor_->schedule(std::move(handle), static_cast<uint64_t>(Executor::Priority::YIELD));
                    }

                    void await_resume() noexcept {}

                private:
                    Executor *executor_;
                };

            public:
                LazyPromiseBase() noexcept : executor_(nullptr), lazy_local_(nullptr) {}
                // Lazily started, coroutine will not execute until first resume() is called
                std::suspend_always initial_suspend() noexcept { return {}; }
                FinalAwaiter final_suspend() noexcept
                {
                    return {};
                }

                template <typename Awaitable>
                auto await_transform(Awaitable &&awaitable)
                {
                    return detail::coAwait(executor_, std::forward<Awaitable>(awaitable));
                }

                // co_await CurrentExecutor and executor_ will be returned directly
                auto await_transform(CurrentExecutor)
                {
                    return ReadyAwaiter<Executor *>(executor_);
                }

                template <typename T>
                auto await_transform(CurrentLazyLocal<T>)
                {
                    return ReadyAwaiter<T *>(lazy_local_ ? dynamicCast<T>(lazy_local_) : nullptr);
                }

                auto await_transform(Yield)
                {
                    return YieldAwaiter(executor_);
                }
                /// IMPORTANT: _continuation should be the first member due to the
                /// requirement of dbg script.
                std::coroutine_handle<> continuation_;
                Executor *executor_;
                LazyLocalBase *lazy_local_;
            };

            template <typename T>
            class LazyPromise : public LazyPromiseBase
            {
            public:
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-alignof-expression"
                static_assert(alignof(T) <= alignof(std::max_align_t),
                              "async_framework doesn't allow Lazy with over aligned object");
#endif
                LazyPromise() noexcept {}
                ~LazyPromise() noexcept {}
                Lazy<T> get_return_object() noexcept;

                static Lazy<T> get_return_object_on_allocation_failure() noexcept;

                template <typename V>
                void return_value(V &&value) noexcept(std::is_nothrow_constructible_v<T, V &&>)
                    requires std::is_convertible_v<V &&, T>
                {
                    value_.template emplace<T>(std::forward<V>(value));
                }

                void unhandled_exception() noexcept
                {
                    value_.template emplace<std::exception_ptr>(std::current_exception());
                }

            public:
                T &result() &
                {
                    if (std::holds_alternative<std::exception_ptr>(value_))
                    {
                        AS_UNLIKELY
                        {
                            std::rethrow_exception(std::get<std::exception_ptr>(value_));
                        }
                    }
                    assert(std::holds_alternative<T>(value_));
                    return std::get<T>(value_);
                }

                T &&result() &&
                {
                    if (std::holds_alternative<std::exception_ptr>(value_))
                    {
                        AS_UNLIKELY
                        {
                            std::rethrow_exception(std::get<std::exception_ptr>(value_));
                        }
                    }
                    assert(std::holds_alternative<T>(value_));
                    return std::move(std::get<T>(value_));
                }

                Try<T> tryResult() noexcept
                {
                    if (std::holds_alternative<std::exception_ptr>(value_))
                    {
                        AS_UNLIKELY
                        {
                            return Try<T>(std::get<std::exception_ptr>(value_));
                        }
                    }
                    else
                    {
                        assert(std::holds_alternative<T>(value_));
                        return Try<T>(std::move(std::get<T>(value_)));
                    }
                }
                std::variant<std::monostate, T, std::exception_ptr> value_;
            };

            template <>
            class LazyPromise<void> : public LazyPromiseBase
            {
            public:
                LazyPromise() noexcept {}
                ~LazyPromise() noexcept {}

                Lazy<void> get_return_object() noexcept;
                static Lazy<void> get_return_object_on_allocation_failure() noexcept;

                void return_void() noexcept {}
                void unhandled_exception() noexcept
                {
                    exception_ = std::current_exception();
                }

                void result()
                {
                    if (exception_ != nullptr)
                        AS_UNLIKELY
                        {
                            std::rethrow_exception(exception_);
                        }
                }

            public:
                std::exception_ptr exception_{nullptr};
            };
        } // namespace detail

        template <typename T>
        class RescheduleLazy;

        namespace detail
        {
            template <typename T>
            struct LazyAwaiterBase
            {
                using Handle = std::coroutine_handle<LazyPromise<T>>;
                Handle handle_;
                LazyAwaiterBase(LazyAwaiterBase &other) = delete;
                LazyAwaiterBase &operator=(LazyAwaiterBase &other) = delete;

                LazyAwaiterBase(LazyAwaiterBase &&other) : handle_(std::exchange(other.handle_, nullptr)) {}
                LazyAwaiterBase &operator=(LazyAwaiterBase &&other)
                {
                    std::swap(handle_, other.handle_);
                    return *this;
                }
                LazyAwaiterBase(Handle coro) : handle_(coro) {}
                ~LazyAwaiterBase()
                {
                    if (handle_)
                    {
                        handle_.destroy();
                        handle_ = nullptr;
                    }
                }

                bool await_ready() const noexcept { return false; }
                auto awaitResume()
                {
                    if constexpr (std::is_void_v<T>)
                    {
                        handle_.promise().result();
                        // We need to destroy the handle expclictly since the awaited
                        // coroutine after symmetric transfer couldn't release it self any
                        // more.
                        handle_.destroy();
                        handle_ = nullptr;
                    }
                    else
                    {
                        auto r = std::move(handle_.promise()).result();
                        handle_.destroy();
                        handle_ = nullptr;
                        return r;
                    }
                }

                Try<T> awaitResumeTry() noexcept
                {
                    Try<T> ret = handle_.promise().tryResult();
                    handle_.destroy();
                    handle_ = nullptr;
                    return ret;
                }
            };

            template <typename T, typename CB>
            concept isLazyCallback = std::is_invocable_v<CB, Try<T>>;

            template <typename T, bool reschedule>
            class LazyBase
            {
            public:
                using promise_type = detail::LazyPromise<T>;
                using Handle = std::coroutine_handle<promise_type>;
                using ValueType = T;

                struct AwaiterBase : public detail::LazyAwaiterBase<T>
                {
                    using Base = LazyAwaiterBase<T>;
                    AwaiterBase(Handle coro) : Base(coro) {}

                    template <typename PromiseType>
                    AS_INLINE auto await_suspend(std::coroutine_handle<PromiseType> continuation)
                    {
                        static_assert(std::is_base_of_v<LazyPromiseBase, PromiseType> || std::is_same_v<detail::DetachedCoroutine::promise_type, PromiseType>,
                                      "'co_await Lazy' is only allowed to be called by Lazy or DetachedCoroutine");
                        // current coro started, caller becomes my continuation
                        this->handle_.promise().continuation_ = continuation;
                        if constexpr (std::is_base_of_v<LazyPromiseBase, PromiseType>)
                        {
                            auto *&local = this->handle_.promise().lazy_local_;
                            logicAssert(local == nullptr || continuation.promise().lazy_local_ == nullptr, "we don't allowed set lazy local twice or co_await a lazy with local value");
                            if (local == nullptr)
                            {
                                local = continuation.promise().lazy_local_;
                            }
                        }
                        return awaitSuspendImpl();
                    }

                private:
                    auto awaitSuspendImpl() noexcept(!reschedule)
                    {
                        if constexpr (reschedule)
                        {
                            auto &pr = this->handle_.promise();
                            logicAssert(pr.executor_, "RescheduleLazy need executor");
                            pr.executor_->schedule(this->handle_);
                        }
                        else
                        {
                            return this->handle_;
                        }
                    }
                };

                struct TryAwaiter : public AwaiterBase
                {
                    TryAwaiter(Handle coro) : AwaiterBase(coro) {}
                    AS_INLINE Try<T> await_resume() noexcept
                    {
                        return AwaiterBase::awaitResumeTry();
                    }

                    auto coAwait(Executor *ex)
                    {
                        if constexpr (reschedule)
                        {
                            logicAssert(fasle, "RescheduleLazy should be only allowed in DetachedCoroutine");
                        }
                        // derived lazy inherits executor
                        this->handle_.promise().executor_ = ex;
                        return std::move(*this);
                    }
                };

                struct ValueAwaiter : public AwaiterBase
                {
                    ValueAwaiter(Handle coro) : AwaiterBase(coro) {}
                    AS_INLINE T await_resume()
                    {
                        return AwaiterBase::awaitResume();
                    }
                };

                ~LazyBase()
                {
                    if (coro_)
                    {
                        coro_.destroy();
                        coro_ = nullptr;
                    }
                }

                explicit LazyBase(Handle coro) noexcept : coro_(coro) {}
                LazyBase(LazyBase &&other) noexcept : coro_(std::move(other.coro_))
                {
                    other.coro_ = nullptr;
                }

                LazyBase(const LazyBase &) = delete;
                LazyBase &operator=(const LazyBase &) = delete;

                Executor *getExecutor()
                {
                    return coro_.promise().executor_;
                }

                template <typename F>
                void start(F &&callback)
                    requires(isLazyCallback<T, F>)
                {
                    logicAssert(this->coro_.operator bool(), "Lazy do not have a coroutine_handle. Maybe the allocation failed or you're using a used Lazy");
                    // callback should take a single Try<T> as parameter, return value will
                    // be ignored. a detached coroutine will not suspend at initial/final
                    // suspend point.
                    auto launchCoro = [](LazyBase lazy, std::decay_t<F> cb) -> detail::DetachedCoroutine
                    {
                        cb(co_await lazy.coAwaitTry());
                    };
                    [[maybe_unused]] auto detached = launchCoro(std::move(*this), std::forward<F>(callback));
                }

                bool isReady() const
                {
                    return !coro_ || coro_.done();
                }

                auto operator co_await()
                {
                    return ValueAwaiter(std::exchange(coro_, nullptr));
                }

                auto coAwaitTry()
                {
                    return TryAwaiter(std::exchange(coro_, nullptr));
                }

            protected:
                Handle coro_;

                template <typename, typename OAlloc, bool Para>
                friend struct detail::CollectAllAwaiter;

                template <bool, template <typename> typename, typename...>
                friend struct detail::CollectAllVariadicAwaiter;

                template <typename LazyType, typename IAlloc, typename Callback>
                friend struct detail::CollectAnyAwaiter;

                template <template <typename> typename LazyType, typename... Ts>
                friend struct detail::CollectAnyVariadicAwaiter;

                template <typename... Ts>
                friend struct detail::CollectAnyVariadicPairAwaiter;
            };
        } // namespace detail

        // Lazy is a coroutine task which would be executed lazily.
        // The user who wants to use Lazy should declare a function whose return type
        // is Lazy<T>. T is the type you want the function to return originally.
        // And if the function doesn't want to return any thing, use Lazy<>.
        //
        // Then in the function, use co_return instead of return. And use co_await to
        // wait things you want to wait. For example:
        //
        // ```C++
        //  // Return 43 after 10s.
        //  Lazy<int> foo() {
        //     co_await sleep(10s);
        //     co_return 43;
        // }
        // ```
        //
        // To get the value wrapped in Lazy, we could co_await it like:
        //
        // ```C++
        //  Lazy<int> bar() {
        //      // This would return the value foo returned.
        //      co_return co_await foo();
        // }
        // ```
        //
        // If we don't want the caller to be a coroutine too, we could use Lazy::start
        // to get the value asynchronously.
        //
        // ```C++
        // void foo_use() {
        //     foo().start([](Try<int> &&value){
        //         std::cout << "foo: " << value.value() << "\n";
        //     });
        // }
        // ```
        //
        // When the foo gets its value, the value would be passed to the lambda in
        // Lazy::start().
        //
        // If the user wants to get the value synchronously, he could use
        // async_framework::coro::syncAwait.
        //
        // ```C++
        // void foo_use2() {
        //     auto val = async_framework::coro::syncAwait(foo());
        //     std::cout << "foo: " << val << "\n";
        // }
        // ```
        //
        // There is no executor instance in a Lazy. To specify an executor to schedule
        // the execution of the Lazy and corresponding Lazy tasks inside, user could use
        // `Lazy::via` to assign an executor for this Lazy. `Lazy::via` would return a
        // RescheduleLazy. User should use the returned RescheduleLazy directly. The
        // Lazy which called `via()` shouldn't be used any more.
        //
        // If Lazy is co_awaited directly, sysmmetric transfer would happend. That is,
        // the stack frame for current caller would be released and the lazy task would
        // be resumed directly. So the user needn't to worry about the stack overflow.
        //
        // The co_awaited Lazy shouldn't be accessed any more.
        //
        // When a Lazy is co_awaited, if there is any exception happened during the
        // process, the co_awaited expression would throw the exception happened. If the
        // user does't want the co_await expression to throw an exception, he could use
        // `Lazy::coAwaitTry`. For example:
        //
        //  ```C++
        //      Try<int> res = co_await foo().coAwaitTry();
        //      if (res.hasError())
        //          std::cout << "Error happend.\n";
        //      else
        //          std::cout << "We could get the value: " << res.value() << "\n";
        // ```
        //
        // If any awaitable wants to derive the executor instance from its caller, it
        // should implement `coAwait(Executor*)` member method. Then the caller would
        // pass its executor instance to the awaitable.

        template <typename T>
        concept isDerivedFromLazyLocal = std::is_base_of_v<LazyLocalBase, T> && requires(const T *base) {
            std::is_same_v<decltype(T::classof(base)), bool>;
        };

        template <typename T = void>
        class [[nodiscard]] CORO_ONLY_DESTROY_WHEN_DONE ELIDEABLE_AFTER_AWAIT Lazy : public detail::LazyBase<T, /*reschedule=*/false>
        {
            using Base = detail::LazyBase<T, false>;
            template <isDerivedFromLazyLocal LazyLocal>
            static Lazy<T> setLazyLocalImpl(Lazy<T> self, LazyLocal local)
            {
                self.coro_.promise().lazy_local_ = &local;
                co_return co_await std::move(self);
            }

            template <isDerivedFromLazyLocal LazyLocal>
            static Lazy<T> setLazyLocalImpl(Lazy<T> self, std::unique_ptr<LazyLocal> base)
            {
                self.coro_.promise().lazy_local_ = base.get();
                co_return co_await std::move(self);
            }

            template <isDerivedFromLazyLocal LazyLocal>
            static Lazy<T> setLazyLocalImpl(Lazy<T> self, std::shared_ptr<LazyLocal> base)
            {
                self.coro_.promise().lazy_local_ = base.get();
                co_return co_await std::move(self);
            }

        public:
            using Base::Base;

            // Bind an executor to a Lazy, and convert it to RescheduleLazy.
            // You can only call via on rvalue, i.e. a Lazy is not accessible after
            // via() was called.
            RescheduleLazy<T> via(Executor *ex) &&
            {
                logicAssert(this->coro_.operator bool(), "Lazy do not have a coroutine_handle. May be the allocation failed or you're using a used Lazy");
                this->coro_.promise().executor_ = ex;

                return RescheduleLazy<T>(std::exchange(this->coro_, nullptr));
            }

            // Bind an executor only. Don't re-schedule.
            //
            // This function is deprecated, please use start(cb, ex) instead of setEx.
            [[deprecated]] Lazy<T> setEx(Executor *ex) &&
            {
                logicAssert(this->coro_.operator bool(), "Lazy do not have a coroutine_handle. May be the allocation failed or you're using a used Lazy");
                this->coro_.promise().executor_ = ex;
                return Lazy<T>(std::exchange(this->coro_, nullptr));
            }

            template <isDerivedFromLazyLocal LazyLocal>
            Lazy<T> setLazyLocal(std::unique_ptr<LazyLocal> base) &&
            {
                return setLazyLocalImpl(std::move(*this), std::move(base));
            }

            template <isDerivedFromLazyLocal LazyLocal>
            Lazy<T> setLazyLocal(std::shared_ptr<LazyLocal> base) &&
            {
                return setLazyLocalImpl(std::move(*this), std::move(base));
            }

            template <isDerivedFromLazyLocal LazyLocal, typename... Args>
            Lazy<T> setLazyLocal(Args &&...args) &&
            {
                logicAssert(this->coro_ operator bool(), "Lazy do not have a coroutine handle. Maybe the allocation failed or you're using a used Lazy");
                if constexpr (std::is_move_constructible_v<LazyLocal>)
                {
                    return setLazyLocalImpl<LazyLocal>(std::move(*this), LazyLocal{std::forward<Args>(args)...});
                }
                else
                {
                    return setLazyLocalImpl<LazyLocal>(std::move(*this), std::make_unique<LazyLocal>(std::forward<Args>(args)...));
                }
            }

            // Bind an executor and start coroutine without scheduling immediately.
            template <typename F>
            void directlyStart(F &&callback, Executor *executor)
                requires(detail::isLazyCallback<T, F>)
            {
                this->coro_.promise().executor_ = executor;
                return start(std::forward<F>(callback));
            }

            auto coAwait(Executor *ex)
            {
                logicAssert(this->coro_.operator bool(), "Lazy do not have a coroutine_handle. Maybe the allocation failed or you're using a used Lazy.");
                // derived lazy inherits executor
                this->coro_.promise().executor_ = ex;
                return typename Base::ValueAwaiter(std::exchange(this->coro_, nullptr));
            }

        private:
            friend class RescheduleLazy<T>;
        };

        // dispatch a lazy to executor, dont reschedule immediately

        // A RescheduleLazy is a Lazy with an executor. The executor of a RescheduleLazy
        // wouldn't/shouldn't be nullptr. So we needn't check it.
        //
        // The user couldn't/shouldn't declare a coroutine function whose return type is
        // RescheduleLazy. The user should get a RescheduleLazy by a call to
        // `Lazy::via(Executor)` only.
        //
        // Different from Lazy, when a RescheduleLazy is co_awaited/started/syncAwaited,
        // the RescheduleLazy wouldn't be executed immediately. The RescheduleLazy would
        // submit a task to resume the corresponding Lazy task to the executor. Then the
        // executor would execute the Lazy task later.

        template <typename T = void>
        class [[nodiscard]] RescheduleLazy : public detail::LazyBase<T, /*reschedule=*/true>
        {
            using Base = detail::LazyBase<T, true>;

        public:
            void detach()
            {
                this->start([](auto &&t)
                            {
                    if(t.hasError()){
                        std::rethrow_exception(t.getException());
                    } });
            }

            [[deprecated("RescheduleLazy should be only allowed in DetachedCoroutine")]] auto
            operator co_await()
            {
                return Base::operator co_await();
            }

        private:
            using Base::Base;
        };

        template <typename T>
        inline Lazy<T> detail::LazyPromise<T>::get_return_object() noexcept
        {
            return Lazy<T>(Lazy<T>::Handle::from_promise(*this));
        }

        inline Lazy<void> detail::LazyPromise<void>::get_return_object() noexcept
        {
            return Lazy<void>(Lazy<void>::Handle::from_promise(*this));
        }

        /// Why do we want to introduce `get_return_object_on_allocation_failure()`?
        /// Since a coroutine will be roughly converted to:
        ///
        /// ```C++
        /// void *frame_addr = ::operator new(required size);
        /// __promise_ = new (frame_addr) __promise_type(...);
        /// __return_object_ = __promise_.get_return_object();
        /// co_await __promise_.initial_suspend();
        /// try {
        ///     function-body
        /// } catch (...) {
        ///     __promise_.unhandled_exception();
        /// }
        /// co_await __promise_.final_suspend();
        /// ```
        ///
        /// Then we can find that the coroutine should be nounwind (noexcept) naturally
        /// if the constructor of the promise_type, the get_return_object() function,
        /// the initial_suspend, the unhandled_exception(), the final_suspend and the
        /// allocation function is noexcept.
        ///
        /// For the specific coroutine type, Lazy, all the above except the allocation
        /// function is noexcept. So that we can make every Lazy function noexcept
        /// naturally if we make the allocation function nothrow. This is the reason why
        /// we want to introduce `get_return_object_on_allocation_failure()` to Lazy.
        ///
        /// Note that the optimization may not work in some platforms due the ABI
        /// limitations. Since they need to consider the case that the destructor of an
        /// exception can throw exceptions.

        template <typename T>
        inline Lazy<T> detail::LazyPromise<T>::get_return_object_on_allocation_failure() noexcept
        {
            return Lazy<T>(typename Lazy<T>::Handle(nullptr));
        }

        inline Lazy<void> detail::LazyPromise<void>::get_return_object_on_allocation_failure() noexcept
        {
            return Lazy<void>(Lazy<void>::Handle(nullptr));
        }
    } // namespace coro
} // namespace async_framework