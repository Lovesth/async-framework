#ifndef ASYNC_FRAMEWORK_TRAITS_H
#define ASYNC_FRAMEWORK_TRAITS_H

#include "Try.h"

namespace async_framework
{
    template <typename T>
    class Future;

    template <typename T>
    struct IsFuture : std::false_type
    {
        using Inner = T;
    };

    template <typename T>
    struct IsFuture<Future<T>> : std::true_type
    {
        using Inner = T;
    };

    template <typename T, typename F>
    struct TryCallableResult
    {
        using Result = std::invoke_result_t<F, Try<T> &&>;
        using ReturnsFuture = IsFuture<Result>;
        static constexpr bool isTry = true;
    };

    template <typename T, typename F>
    struct ValueCallableResult
    {
        using Result = std::invoke_result_t<F>;
        using ReturnsFuture = IsFuture<Result>;
        static constexpr bool isTry = false;
    };

    namespace detail
    {
        template <typename T>
        struct remove_cvref
        {
            using type = typename std::remove_cv<typename std::remove_reference_t<T>>;
        };

        template <typename T>
        using remove_cvref_t = typename remove_cvref<T>::type;
    } // namespace detail

} // namespace async_framework

#endif