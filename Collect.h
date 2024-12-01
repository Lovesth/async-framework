#pragma once

#include <iterator>
#include <vector>
#include "Try.h"
#include <future>

namespace async_framework
{
    // collectAll - collect all the values for a range of futures.
    //
    // The arguments include a begin iterator and a end iterator.
    // The arguments specifying a range for the futures to be collected.
    //
    // For a range of `Future<T>`, the return type of collectAll would
    // be `Future<std::vector<Try<T>>>`. The length of the vector in the
    // returned future is the same with the number of futures inputted.
    // The `Try<T>` in each field reveals that if there is an exception
    // happened during the execution for the Future.
    //
    // This is a non-blocking API. It wouldn't block the execution even
    // if there are futures doesn't have a value. For each Future inputted,
    // if it has a result, the result is forwarded to the corresponding fields
    // of the returned future. If it wouldn't have a result, it would fulfill
    // the corresponding field in the returned future once it has a result.
    //
    // Since the returned type is a future. So the user wants to get its value
    // could use `get()` method synchronously or `then*()` method asynchronously.
    

} // namespace async_framework