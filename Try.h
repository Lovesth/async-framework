#ifndef ASYNC_FRAMEWORK_TRY_H
#define ASYNC_FRAMEWORK_TRY_H
#include <cassert>
#include <execution>
#include <functional>
#include <utility>
#include <variant>
#include "Common.h"
#include "Unit.h"

namespace async_framework
{
    template <typename T>
    // 只是一个声明，定义在下面
    class Try;

    template <>
    // 只是一个声明，定义在下面
    class Try<void>;

    template <typename T>
    class Try
    {
    public:
        Try() = default;
        ~Try() = default;
        Try(Try<T> &&other) = default;

        template <typename T2 = T>
        Try(std::enable_if_t<std::is_same<Unit, T2>::value, const Try<void> &> other)
        {
        }
    };
}

#endif