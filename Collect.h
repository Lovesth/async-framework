#pragma once

#include <iterator>
#include <vector>
#include "Try.h"
#include "Future.h"

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
    template <std::input_iterator Iterator>
    inline Future<std::vector<Try<typename std::iterator_traits<Iterator>::value_type::value_type>>>
    collectAll(Iterator begin, Iterator end)
    {
        using T = typename std::iterator_traits<Iterator>::value_type::value_type;
        size_t n = std::distance(begin, end);

        bool allReady = true;
        for (auto iter = begin; iter != end; ++iter)
        {
            if (!iter->hasResult())
            {
                allReady = false;
                break;
            }
        }
        // n个Iterator都有结果
        if (allReady)
        {
            std::vector<Try<T>> results;
            results.reserve(n);
            for (auto iter = begin; iter != end; ++iter)
            {
                results.push_back(std::move(iter->results()));
            }
            return Future<std::vector<Try<T>>>(std::move(results));
        }

        Promise<std::vector<Try<T>>> promise;
        auto future = promise.getFuture();
        // 使用共享指针，回调，在析构函数里面setValue。
        // 使用共享指针管理Context，只有所有features都设置了value, Context才会销毁，然后
        // 在析构函数里面设置值。
        struct Context
        {
            Context(size_t n, Promise<std::vector<Try<T>>> p_) : results(n), p(std::move(p_)) {}
            ~Context() { p.setValue(std::move(results)); }
            std::vector<Try<T>> results;
            Promise<std::vector<Try<T>>> p;
        };

        auto ctx = std::make_shared<Context>(n, std::move(promise));
        for (size_t i = 0; i < n; ++i, ++begin)
        {
            if (begin->hasResult())
            {
                ctx->results[i] = std::move(begin->result());
            }
            else
            {
                begin->setContinuation([ctx, i](Try<T> &&t) mutable
                                       { ctx->results[i] = std::move(t); });
            }
        }
        return future;
    }

} // namespace async_framework