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
            
        private:
            Future<T> future_;
        };
    } // namespace detail
} // namespace async_framework::coro