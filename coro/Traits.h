#pragma once
#include <utility>

namespace async_framework
{
    namespace coro
    {
        namespace detail
        {
            template <typename T>
            concept HasCoAwaitMethod = requires(T &&awaitable) {
                std::forward<T>(awaitable).coAwait(nullptr);
            };

            template <typename T>
            concept HasMemberCoAwaitOperator = requires(T &&awaitable) {
                std::forward<T>(awaitable).operator co_await();
            };

#ifdef _MSC_VER
            // FIXME: MSVC compiler bug, See
            // https://developercommunity.visualstudio.com/t/10160851
            template <class, class = void>
            struct HasGlobalCoAwaitOperatorHelp : std::false_type
            {
            };

            template <class T>
            struct HasGlobalCoAwaitOperatorHelp<
                T, std::void_t<decltype(operator co_await(std::declval<T>()))>>
                : std::true_type
            {
            };

            template <class T>
            concept HasGlobalCoAwaitOperator = HasGlobalCoAwaitOperatorHelp<T>::value;
#else
            template <class T>
            concept HasGlobalCoAwaitOperator = requires(T &&awaitable) {
                operator co_await(std::forward<T>(awaitable));
            };
#endif
            template <typename Awaitable>
            auto getAwaiter(Awaitable &&awaitable)
            {
                if constexpr (HasMemberCoAwaitOperator<Awaitable>)
                    return std::forward<Awaitable>(awaitable).operator co_await();
                else if constexpr (HasGlobalCoAwaitOperator<Awaitable>)
                    return operator co_await(std::forward<Awaitable>(awaitable));
                else
                    return std::forward<Awaitable>(awaitable);
            }
        } // namesapce detail
    } // namespace coro
} // namespace async_framework