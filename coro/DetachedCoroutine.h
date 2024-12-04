#pragma once

#include <stdio.h>
#include <exception>
#include <coroutine>

namespace async_simple
{
    namespace coro
    {
        namespace detail
        {
            // A detached coroutine. It would start to execute
            // immediately and throws the exception it met.
            // This could be used as the root of a coroutine
            // execution chain.
            //
            // But the user shouldn't use this directly. It may be
            // better to use `Lazy::start()`.
        }
    }
} //