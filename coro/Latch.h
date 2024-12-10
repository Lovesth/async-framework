#pragma once

#include <cassert>
#include <cstddef>
#include "./ConditionVariable.h"
#include "./Lazy.h"
#include "./SpinLock.h"

namespace async_framework::coro
{
	// The latch class is a downward counter of type std::size_t which can be
	// used to synchronize coroutines. The value of the counter is initialized on
	// creation. Coroutines may block on the latch until the counter is decremented
	// to zero. It will suspend the current coroutine and switch to other coroutines
	// to run.
	// There is no possibility to increase or reset the counter, which
	// makes the latch a single-use barrier.
	class Latch
	{
	public:
		explicit Latch(std::size_t count) : count_(count) {}
		~Latch() = default;
		Latch &operator=(const Latch &) = delete;

		// decrease the counter in a non-blocking manner
		Lazy<void> count_down(std::size_t update = 1)
		{
			auto lk = co_await mutex_.coScopedLock();
			assert(count_ >= update);
			count_ -= update;
			if (!count_)
			{
				cv_.notify();
			}
		}

		// tests if the internal counter equals zero
		Lazy<bool> try_wait() const noexcept
		{
			auto lk = co_await mutex_.coScopedLock();
			co_return !count_;
		}

		// blocks until the counter reaches zero
		// If the counter is not 0, the current coroutine will be suspended
		Lazy<void> wait() const noexcept
		{
			auto lk = co_await mutex_.coScopedLock();
			co_await cv_.wait(mutex_, [&]
							  { return count_ == 0; });
		}

		// decrease the counter and blocks until it reaches zero
		Lazy<void> arrive_and_wait(std::size_t update = 1) noexcept
		{
			co_await count_down(update);
			co_await wait();
		}

	private:
		using MutexType = SpinLock;
		mutable MutexType mutex_;
		mutable ConditionVariable<MutexType> cv_;
		std::size_t count_;
	};

}
