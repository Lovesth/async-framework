#pragma once

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
            if (other.hasError())
            {
                value_.tmplate emplace<std::exception_ptr>(other._error);
            }
            else
            {
                value_.template emplace<T>();
            }
        }

        Try &operator=(Try<T> &&other) = default;
        Try &operator=(std::exception_ptr error)
        {
            if (std::holds_alternative<std::exception_ptr>(value_))
            {
                std::get<std::exception_ptr>(value_) = error;
            }
            else
            {
                value_.template emplace<std::exception_ptr>(error);
            }
            return *this;
        }

        template <typename... U>
        Try(U &&...value)
            requires std::is_constructible_v<T, U...>
            : value_(std::in_place_type<T>, std::forward<U>(value)...)
        {
        }

        Try(std::exception_ptr error) : value_(error) {}

    private:
        Try(const Try &) = delete;
        Try &operator=(const Try &) = delete;

    public:
        constexpr bool available() const noexcept
        {
            return !hasError();
        }

        constexpr bool hasError() const noexcept
        {
            return std::holds_alternative<std::exception_ptr>(value_);
        }

        const T &value() const &
        {
            checkHasTry();
            return std::get<T>(value_);
        }

        T &value() &
        {
            checkHasTry();
            std::get<T>(value_);
        }

        // 这里一个右值调用value，所以直接将其保存的值move返回
        const T &&value() const &&
        {
            checkHasTry();
            std::move(std::get<T>(value_));
        }

        T &&value() &&
        {
            checkHasTry();
            std::move(std::get<T>(value_));
        }

        template <typename... Args>
        T &emplace(Args &&...args)
        {
            return value_.template emplace<T>(std::forward<Args>(args)...);
        }

        void setException(std::exception_ptr error)
        {
            if (std::holds_alternative<std::exception_ptr>(value_))
            {
                std::get<std::exception_ptr>(value_) = error;
            }
            else
            {
                value_.template emplace<std::exception_ptr>(error);
            }
        }

        std::exception_ptr getException() const
        {
            logicAssert(std::holds_alternative<std::exception_ptr>(value_), "Try object doesn't has an error");
            return std::get<std::exception_ptr>(value_);
        }

        operator Try<void>() const;

    private:
        AS_INLINE void checkHasTry() const
        {
            if (std::holds_alternative<T>(value_))
            {
                AS_LIKELY { return; }
            }
            else if (std::holds_alternative<std::exception_ptr>(value_))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(value_));
            }
            else if (std::holds_alternative<std::monostate>(value_))
            {
                throw std::logic_error("Try object is empty");
            }
            else
            {
                assert(false);
            }
        }

    private:
        std::variant<std::monostate, T, std::exception_ptr> value_;

    private:
        friend Try<Unit>;
    };

    template <>
    class Try<void>
    {
    public:
        Try() {};
        Try(std::exception_ptr error) : error_(error) {}
        Try &operator=(std::exception_ptr error)
        {
            error_ = error;
            return *this;
        }

        Try(Try &&other) : error_(std::move(other.error_)) {}

        Try &operator=(Try &&other)
        {
            if (this != &other)
            {
                std::swap(error_, other.error_);
            }
            return *this;
        }

        void value()
        {
            if (error_)
            {
                std::rethrow_exception(error_);
            }
        }

        bool hasError()
        {
            return error_.operator bool();
        }

        void setException(std::exception_ptr error)
        {
            error_ = error;
        }

        std::exception_ptr getError()
        {
            return error_;
        }

    private:
        std::exception_ptr error_;

    private:
        friend Try<Unit>;
    };

    template <typename T>
    Try<T>::operator Try<void>() const
    {
        if (hasError())
        {
            return Try<void>(std::get<std::exception_ptr>(value_));
        }
        return Try<void>{};
    }

    // self defined deduction guide
    // 根据构造函数参数来推导类模板参数
    template <typename T>
    Try(T) -> Try<T>;

    // 后置...用于模板参数或者函数参数包展开
    // typename和sizeof使用前置...
    template <typename F, typename... Args>
    auto makeTryCall(F &&f, Args... args)
    {
        using T = std::invoke_result_t<F, Args...>;
        try
        {
            if constexpr (std::is_same_v<T, void>)
            {
                std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
                return Try<void>{};
            }
            else
            {
                return Try<T>(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
            }
        }
        catch
        {
            return Try<T>(std::current_exception());
        }
    }
} // namespace async_framework