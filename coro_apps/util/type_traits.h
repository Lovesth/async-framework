#pragma once
#include <functional>
#include <memory>
#include <type_traits>

#ifdef __GLIBCXX__
#if __GLIBCXX__ < 20200825
namespace std
{
    template <typename T>
    struct remove_cvref
    {
        using type = std::remove_cv_t<std::remove_reference_t<T>>;
    };
    template <typename T>
    using remove_cvref_t = typename remove_cvref<T>::type;
}
#endif
#endif

namespace util
{
    template <typename Function>
    struct function_traits;

    template <typename Return, typename... Arguments>
    struct function_traits<Return (*)(Arguments...)>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
    };

    template <typename Return, typename... Arguments>
    struct function_traits<Return (*)(Arguments...) noexcept>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
    };

    template <typename Return, typename... Arguments>
    struct function_traits<Return(Arguments...)>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
    };

    template <typename Return, typename... Arguments>
    struct function_traits<Return(Arguments...) noexcept>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
    };

    template <typename This, typename Return, typename... Arguments>
    struct function_traits<Return (This::*)(Arguments...)>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
        using
    };

    template <typename This, typename Return, typename... Arguments>
    struct function_traits<Return (This::*)(Arguments...) noexcept>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
        using class_type = This;
    };

    template <typename This, typename Return, typename... Arguments>
    struct function_traits<Return (This::*)(Arguments...) const>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
        using class_type = This;
    };

    template <typename This, typename Return, typename... Arguments>
    struct function_traits<Return (This::*)(Arguments...) const noexcept>
    {
        using parameters_type = std::tuple<std::remove_cvref_t<Arguments>...>;
        using return_type = Return;
        using class_type = This;
    };

    template <typename Return>
    struct function_traits<Return (*)()>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename Return>
    struct function_traits<Return (*)() noexcept>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename Return>
    struct function_traits<Return (&)()>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename Return>
    struct function_traits<Return (&)() noexcept>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename Return>
    struct function_traits<Return()>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename Return>
    struct function_traits<Return() noexcept>
    {
        using parameters_type = void;
        using return_type = Return;
    };

    template <typename This, typename Return>
    struct function_traits<Return (This::*)()>
    {
        using parameters_type = void;
        using return_type = Return;
        using class_type = This;
    };

    template <typename This, typename Return>
    struct function_traits<Return (This::*)() noexcept>
    {
        using parameters_type = void;
        using return_type = Return;
        using class_type = This;
    };

    template <typename This, typename Return>
    struct function_traits<Return (This::*)() const>
    {
        using parameters_type = void;
        using return_type = Return;
        using class_type = This;
    };

    template <typename This, typename Return>
    struct function_traits<Return (This::*)() const noexcept>
    {
        using parameters_type = void;
        using return_type = Return;
        using class_type = This;
    };

    // support function object and lambda expression
    template <typename Function>
    struct function_traits : function_traits<decltype(&Function::operator())>
    {
    };

    template <typename Function>
    using function_parameters_t = typename function_traits<std::remove_cvref_t<Function>>::parameters_type;

    // tuple_element_t<idx, tupleType>;
    // tuple_size_v<tupleType>;
    template <typename Function>
    using last_parameters_type_t = std::tuple_element_t<std::tuple_size_v<function_parameters_t<Function>> - 1, function_parameters_t<Function>>;

    template <typename Function>
    using function_return_type_t = typename function_traits<std::remove_cvref_t<Function>>::return_type;

    template <typename Function>
    using class_type_t = typename function_traits<std::remove_cvref_t<Function>>::class_type;

    template <typename F, typename... Args>
    struct is_invocable : std::is_constructible<std::function<void(std::remove_reference_t<Args>...)>,
                                                std::reference_wrapper<std::remove_reference_t<F>>>
    {
    };

    template <typename F, typename... Args>
    inline constexpr bool is_invocable_v = is_invocable<F, Args...>::value;

    template <typename T>
    struct remove_first
    {
        using type = T;
    };

    template <typename First, typename... Second>
    struct remove_first<std::tuple<First, Second...>>
    {
        using type = std::tuple<Second...>;
    };

    template <typename T>
    using remove_first_t = typename remove_first<T>::type;

    // T is tuple type.
    template <bool has_conn, typename T>
    inline auto get_args()
    {
        if constexpr (has_conn)
        {
            using args_type = remove_first_t<T>;
            return args_type{};
        }
        else
        {
            return T{};
        }
    }

    template <typename Test, template <typename...> typename Ref>
    struct is_specialization : std::false_type
    {
    };

    template <template <typename...> typename Ref, typename... Args>
    struct is_specialization<Ref<Args...>, Ref> : std::true_type
    {
    };

    template <typename Test, template <typename...> typename Ref>
    inline constexpr bool is_specialization_v = is_specialization<Test, Ref>::value;
} // namespace util