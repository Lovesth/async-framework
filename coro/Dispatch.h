#pragma once

#include "./Common.h"
#include "./Executor.h"
#include "./Lazy.h"
#include <coroutine>

#include <cassert>
#include <type_traits>

namespace async_framework
{
    namespace coro
    {
        namespace detail
        {
            // based on c++ coroutine common abi.
            // it is not supproted that the PromiseType's align is larger than the size of
            // two function pointer.
            inline std::coroutine_handle<> GetContinuationFromHandle(std::coroutine_handle<> h)
            {
                constexpr size_t promise_offset = 2 * sizeof(void *);
                char *ptr = static_cast<char *>(h.address());
                ptr = ptr + promise_offset;
                return std::coroutine_handle<>::from_address(*static_cast<void **>(static_cast<void *>(ptr)));
            }

            inline void ChangeLaziessExecutorTo(std::coroutine_handle<> h, Executor *ex)
            {
                while (true)
                {
                    std::coroutine_handle<> continuation = GetContinuationFromHandle(h);
                    if (!continuation.address())
                    {
                        break;
                    }
                    auto &promise = std::coroutine_handle<LazyPromiseBase>::from_address(h.address()).promise();
                    promise.executor_ = ex;
                    h = continuation;
                }
            }

            class DispatchAwaiter
            {
            public:
                explicit DispatchAwaiter(Executor *ex) noexcept : ex_(ex)
                {
                    assert(ex_ != nullptr);
                }

                bool await_ready() const noexcept
                {
                    return false;
                }

                template <typename PromiseType>
                bool await_suspend(std::coroutine_handle<PromiseType> h)
                {
                    static_assert(std::is_base_of<LazyPromiseBase, PromiseType>::value, "dispatch is only allowed to be called by Lazy");
                    if (h.promise().executor_ == ex_)
                    {
                        return false;
                    }
                    Executor *old_ex = h.promise().executor_;
                    ChangeLaziessExecutorTo(h, ex_);
                    bool succ = ex_->schedule(std::move(h));
                    // cannot access *this after schedule.
                    // If the scheduling fails, we must change the executor back to its
                    // original value, as the user may catch exceptions and handle them
                    // themselves, which can result in inconsistencies between the executor
                    // recorded by Lazy and the actual executor running.
                    if (succ == false)
                        AS_UNLIKELY
                        {
                            ChangeLaziessExecutorTo(h, old_ex);
                            throw std::runtime_error("dispatch to executor failed");
                        }
                    return true;
                }

                void await_resume() noexcept
                {
                }

            private:
                Executor *ex_;
            };

            class DispatchAwaitable
            {
            public:
                explicit DispatchAwaitable(Executor *ex) : ex_(ex) {}

                auto coAwait(Executor *)
                {
                    return DispatchAwaiter(ex_);
                }

            private:
                Executor *ex_;
            };
        } // namespace detail

        inline detail::DispatchAwaitable diapatch(Executor *ex)
        {
            logicAssert(ex != nullptr, "dispatch's param should not be nullptr");
            return detail::DispatchAwaitable(ex);
        }

    } // namespace coro
} // namespace async_framework