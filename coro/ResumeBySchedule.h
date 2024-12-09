#include "../Executor.h"
#include "../Future.h"
#include "./Lazy.h"
#include <coroutine>

#include <type_traits>
#include <utility>

namespace async_framework::coro
{
    namespace detail
    {
        template <typename T>
        class FutureResumeByScheduleAwaiter
        {
        public:
            FutureResumeByScheduleAwaiter(Future<T> &&f) : future_(std::move(f)) {}
            bool await_ready() { return future_.hasResult(); }

            template <typename PromiseType>
            void await_suspend(std::coroutine_handle<PromiseType> continuation)
            {
                static_assert(std::is_base_of_v<LazyPromiseBase, PromiseType>, "FutureResumeByScheduleAwaiter is only allowed to be called by Lazy");
                Executor *ex = continuation.promise().executor_;
                future_.setContinuation([continuation, ex](Try<T> &&t) mutable
                                        {
                    if(ex != nullptr){
                        ex->schedule(continuation);
                    }else{
                        continuation.resume();
                    } });
            }
            auto await_resume()
            {
                return std::move(future_.value());
            }

        private:
            Future<T> future_;
        };

        template <typename T>
        class FutureResumeByScheduleAwaitable
        {
        public:
            explicit FutureResumeByScheduleAwaitable(Future<T> &&f) : future_(std::move(f))
            {
            }

            auto coAwait(Executor *)
            {
                return FutureResumeByScheduleAwaiter(std::move(future_));
            }

        private:
            Future<T> future_;
        };
    } // namespace detail

    template <typename T>
    inline auto ResumeBySchedule(Future<T> &&future)
    {
        return detail::FutureResumeByScheduleAwaitable<T>(std::move(future));
    }
} // namespace async_framework::coro