#pragma once

#include "../Future.h"
#include "./Lazy.h"
#include <coroutine>

#include <type_traits>

namespace async_framework
{
	namespace coro::detail
	{
		template <typename T>
		struct FutureAwaiter
		{
			Future<T> future_;

			bool await_ready()
			{
				return future_.hasResult();
			}

			template <typename PromiseType>
			void await_suspend(std::coroutine_handle<PromiseType> continuation)
			{
				static_assert(std::is_base_of_v<LazyPromiseBase, PromiseType>, "FutureAwaiter is only allowed to be called by Lazy");
				Executor *ex = continuation.promise().executor_;
				Executor::Context ctx = Executor::NULLCTX;
				if (ex != nullptr)
				{
					ctx = ex->checkout();
				}
				future_.setContinuation([continuation, ex, ctx](Try<T> &&t) mutable
										{
				if(ex != nullptr){
					ex->checkin(continuation, ctx);
				}else{
					continuation.resume();
				} });
			};

			auto await_resume()
			{
				return std::move(future_.value());
			}
		};
	} // namespace detail

	template <typename T>
	auto operator co_await(Future<T> &&future)
	{
		return coro::detail::FutureAwaiter<T>{std::move(future)};
	}

	template <typename T>
	[[deprecated("Require an rvalue future")]]
	auto operator co_await(T &&future)
		requires IsFuture<std::decay_t<T>>::value
	{
		return std::move(operator co_await(std::move(future)));
	}
} // namespace async_framework
