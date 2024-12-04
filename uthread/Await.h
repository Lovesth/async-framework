#pragma once
#include <type_traits>
#include "../Future.h"
#include "../coro/Lazy.h"
#include "../uthread/internal/thread_impl.h"

namespace async_framework
{
    namespace uthread
    {
        // Use to async get future value in uthread context.
        // Invoke await will not block current thread.
        // The current uthread will be suspend until promise.setValue() be called.
        template <typename T>
        T await(Future<T> &&fut)
        {
            logicAssert(fut.valid(), "Future is broken");
            if (fut.hasResult())
            {
                return fut.value();
            }
            auto executor = fut.getExecutor();
            logicAssert(executor, "Future has not a Executor");
            logicAssert(executor->currentThreadInExecutor(), "await invoked not in Executor");

            Promise<T> p;
            auto f = p.getFuture().via(executor);
            p.forceSched().checkout();

            auto ctx = uthread::internal::thread_impl::get();
            f.setContinuation([ctx](auto &&)
                              { uthread::internal::thread_impl::switch_in(ctx); });
            std::move(fut).thenTry([p = std::move(p)](Try<T> &&t) mutable
                                   { p.setValue(std::move(t)); });

            do
            {
                uthread::internal::thread_impl::switch_out(ctx);
                assert(f.hasResult());
            } while (!f.hasResult());
            return f.value();
        }

        // This await interface focus on await function of an object.
        // Here is an example:
        // ```C++
        //  class Foo {
        //  public:
        //     lazy<T> bar(Ts&&...) {}
        //  };
        //  Foo f;
        //  await(ex, &Foo::bar, &f, Ts&&...);
        // ```
        // ```C++
        //  lazy<T> foo(Ts&&...);
        //  await(ex, foo, Ts&&...);
        //  auto lambda = [](Ts&&...) -> lazy<T> {};
        //  await(ex, lambda, Ts&&...);
        // ```
        template <typename Fn, typename... Args>
        decltype(auto) await(Executor *ex, Fn &&fn, Args &&...args)
            requires std::is_invocable_v<Fn &&, Args &&...>
        {
            using ValueType = typename std::invoke_result_t<Fn &&, Args &&...>::ValueType;
            Promise<ValueType> p;
            auto f = p.getFuture().via(ex);
            auto lazy = [p = std::move(p)]<typename... Ts>(Ts &&...ts) mutable -> coro::Lazy<>
            {
                if constexpr (std::is_void_v<ValueType>)
                {
                    co_await std::invoke(std::forward<Ts>(ts)...);
                    p.setValue();
                }
                else
                {
                    p.setValue(co_await std::invoke(std::forward<Ts>(ts)...));
                }
                co_return;
            };
            lazy(std::forward<Fn>(fn), std::forward<Args>(args)...).directlyStart([](auto &&) {}, ex);
            return await(std::move(f));
        }

        // This await interface is special. It would accept the function who receive an
        // argument whose type is `Promise<T>&&`. The usage for this interface is
        // limited. The example includes:
        //
        // ```C++
        //  void foo(Promise<T>&&);
        //  await<T>(ex, foo);
        //  auto lambda = [](Promise<T>&&) {};
        //  await<T>(ex, lambda);
        // ```

        template <typename T, typename Fn>
        T await(Executor *ex, Fn &&fn)
        {
            static_assert(std::is_invocable<decltype(fn), Promise<T>>::value, "Callable of await is not support, eg: Callable(Promise<T>)");
            Promise<T> p;
            auto f = p.getFuture().via(ex);
            p.forceSched().checkout();
            fn(std::move(p));
            return await(std::move(f));
        }
    } // namespace uthread
} // async_framework