#pragma once

#include <array>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "../Common.h"
#include "../Try.h"
#include "../Unit.h"
#include "./CountEvent.h"
#include "./Lazy.h"

namespace async_framework
{
    namespace coro
    {
        // collectAll types
        // auto [x, y] = co_await collectAll(IntLazy, FloatLazy);
        // auto [x, y] = co_await collectAllPara(IntLazy, FloatLazy);
        // std::vector<Try<int>> = co_await collectAll(std::vector<intLazy>);
        // std::vector<Try<int>> = co_await collectAllPara(std::vector<intLazy>);
        // std::vector<Try<int>> = co_await collectAllWindowed(maxConcurrency, yield,
        // std::vector<intLazy>); std::vector<Try<int>> = co_await
        // collectAllWindowedPara(maxConcurrency, yield, std::vector<intLazy>);

        namespace detail
        {
            template <typename T>
            struct CollectAnyResult
            {
                CollectAnyResult() : idx_(static_cast<size_t>(-1)), value_() {}

                CollectAnyResult(size_t idx, std::add_rvalue_reference_t<T> value)

                    requires(!std::is_void_v<T>)
                    :

                      idx_(idx), value_(std::move(value))
                {
                }

                CollectAnyResult(const CollectAnyResult &) = delete;

                CollectAnyResult &operator=(const CollectAnyResult &) = delete;

                CollectAnyResult(CollectAnyResult &&other)
                    : idx_(std::move(other.idx_)), value_(std::move(other.value_))
                {
                    other.idx_ = static_cast<size_t>(-1);
                }

                size_t idx_;
                Try<T> value_;

                size_t index() const { return idx_; }

                bool hasError() const
                {
                    return value_.hasError();
                }

                // Require hasError() == true. Otherwise it is UB to call
                // this method.
                std::exception_ptr getException() const { return value_.getException(); }

// Require hasError() == false. Otherwise it is UB to call
// value() method
#if __cpp_explicit_this_parameter >= 202110L
                // class member has the same cv attributes with this
                template <typename Self>
                auto &&value(this Self &&self)
                {
                    return std::forward<Self>(self).value_.value();
                }
#else

                const T &value() const & { return value_.value(); }

                T &value() & { return value_.value(); }

                T &&value() && { return std::move(value_).value(); }

                const T &&value() const && { return std::move(value_).value(); }

#endif
            };

            template <typename LazyType, typename InAlloc, typename Callback = Unit>
            struct CollectAnyAwaiter
            {
                using ValueType = typename LazyType::ValueType;
                using ResultType = CollectAnyResult<ValueType>;

                CollectAnyAwaiter(std::vector<LazyType, InAlloc> &&input)
                    : input_(std::move(input)), result_(nullptr) {}

                CollectAnyAwaiter(std::vector<LazyType, InAlloc> &&input, Callback callback)
                    : input_(std::move(input)),
                      result_(nullptr),
                      callback_(std::move(callback)) {}

                CollectAnyAwaiter(const CollectAnyAwaiter &) = delete;

                CollectAnyAwaiter &operator=(const CollectAnyAwaiter &) = delete;

                CollectAnyAwaiter(CollectAnyAwaiter &&other)
                    : input_(std::move(other.input_)),
                      result_(std::move(other.result_)),
                      callback_(std::move(other.callback_)) {}

                bool await_ready() const noexcept
                {
                    return input_.empty() ||
                           (result_ && result_->idx_ != static_cast<size_t>(-1));
                }

                void await_suspend(std::coroutine_handle<> continuation)
                {
                    auto promise_type = std::coroutine_handle<LazyPromiseBase>::from_address(
                                            continuation.address())
                                            .promise();
                    auto executor = promise_type.executor_;
                    // we should take care of input's life-time after resume.
                    std::vector<LazyType, InAlloc> input(std::move(input_));
                    // Make local copies to shared_ptr to avoid deleting objects too early
                    // if any coroutine finishes before this function.
                    auto result = std::make_shared<ResultType>();
                    auto event = std::make_shared<detail::CountEvent>(input.size());
                    auto callback = std::move(callback_);

                    result_ = result;
                    for (size_t i = 0;
                         i < input.size() && (result->idx_ == static_cast<size_t>(-1)); ++i)
                    {
                        if (!input[i].coro_.promise().executor_)
                        {
                            input[i].coro_.promise().executor_ = executor;
                        }

                        if constexpr (std::is_same_v<Callback, Unit>)
                        {
                            (void)callback;
                            input[i].start([i, size = input.size(), r = result, c = continuation,
                                            e = event](Try<ValueType> &&result) mutable
                                           {
                                assert(e != nullptr);
                                auto count = e->downCount();
                                if (count == size + 1) {
                                    r->idx_ = i;
                                    r->value_ = std::move(result);
                                    c.resume();
                                } });
                        }
                        else
                        {
                            input[i].start([i, size = input.size(), r = result, c = continuation,
                                            e = event, callback](Try<ValueType> &&result) mutable
                                           {
                                assert(e != nullptr);
                                auto count = e->downCount();
                                if (count == size + 1) {
                                    r->idx_ = i;
                                    (*callback)(i, std::move(result));
                                    c.resume();
                                } });
                        }
                    }
                }

                auto await_resume()
                {
                    if constexpr (std::is_same_v<Callback, Unit>)
                    {
                        assert(result_ != nullptr);
                        return std::move(*result_);
                    }
                    else
                    {
                        return result_->index();
                    }
                }

                std::vector<LazyType, InAlloc> input_;
                std::shared_ptr<ResultType> result_;
                [[no_unique_address]] Callback callback_;
            };

            template <typename... Ts>
            struct CollectAnyVariadicPairAwaiter
            {
                using InputType = std::tuple<Ts...>;

                CollectAnyVariadicPairAwaiter(Ts &&...inputs)
                    : input_(std::move(inputs)...), result_(nullptr) {}

                CollectAnyVariadicPairAwaiter(InputType
                                                  &&inputs)
                    :

                      input_(std::move(inputs)), result_(nullptr)
                {
                }

                CollectAnyVariadicPairAwaiter(const CollectAnyVariadicPairAwaiter &) = delete;

                CollectAnyVariadicPairAwaiter &operator=(const CollectAnyVariadicPairAwaiter &) =
                    delete;

                CollectAnyVariadicPairAwaiter(CollectAnyVariadicPairAwaiter
                                                  &&other)
                    : input_(std::move(other.input_)), result_(std::move(other.result_))
                {
                }

                bool await_ready() const noexcept
                {
                    return result_ && result_->has_value();
                }

                void await_suspend(std::coroutine_handle<> continuation)
                {
                    auto promise_type = std::coroutine_handle<LazyPromiseBase>::from_address(
                                            continuation.address())
                                            .promise();
                    auto executor = promise_type.executor_;
                    auto event =
                        std::make_shared<detail::CountEvent>(std::tuple_size<InputType>());

                    auto result = std::make_shared<std::optional<size_t>>();
                    result_ = result;

                    auto input = std::move(input_);
                    [&]<size_t... I>(std::index_sequence<I...>)
                    {
                        (
                            [&](auto &lazy, auto &callback)
                            {
                                if (result->has_value())
                                {
                                    return;
                                }

                                if (!lazy.coro_.promise().executor_)
                                {
                                    lazy.coro_.promise().executor_ = executor;
                                }

                                lazy.start([result, event, continuation,
                                            callback = std::move(callback)](auto &&res) mutable
                                           {
                                        auto count = event->downCount();
                                        if (count == std::tuple_size<InputType>() + 1) {
                                            callback(std::move(res));
                                            *result = I;
                                            continuation.resume();
                                        } });
                            }(std::get<0>(std::get<I>(input)), std::get<1>(std::get<I>(input))),
                            ...);
                    }(std::make_index_sequence<sizeof...(Ts)>());
                }

                auto await_resume()
                {
                    assert(result_ != nullptr);
                    return std::move(result_->value());
                }

                std::tuple<Ts...> input_;
                std::shared_ptr<std::optional<size_t>> result_;
            };

            template <typename... Ts>
            struct SimpleCollectAnyVariadicPairAwaiter
            {
                using InputType = std::tuple<Ts...>;
                InputType inputs_;

                SimpleCollectAnyVariadicPairAwaiter(Ts &&...inputs)
                    : inputs_(std::move(inputs)...) {}

                auto coAwait(Executor *ex)
                {
                    return CollectAnyVariadicPairAwaiter(std::move(inputs_));
                }
            };

            template <template <typename> typename LazyType, typename... Ts>
            struct CollectAnyVariadicAwaiter
            {
                using ResultType = std::variant<Try<Ts>...>;
                using InputType = std::tuple<LazyType<Ts>...>;

                CollectAnyVariadicAwaiter(LazyType<Ts> &&...inputs)
                    : input_(std::make_unique<InputType>(std::move(inputs))),
                      result_(nullptr) {}

                CollectAnyVariadicAwaiter(InputType &&inputs)
                    : inputs_(std::make_unique<InputType>(std::move(inputs))),
                      result_(nullptr) {}

                CollectAnyVariadicAwaiter(const CollectAnyVariadicAwaiter &) = delete;

                CollectAnyVariadicAwaiter &operator=(const CollectAnyVariadicAwaiter &) = delete;

                CollectAnyVariadicAwaiter(CollectAnyVariadicAwaiter &&other)
                    : input_(std::move(other.input_)), result_(std::move(other.result_)) {}

                bool await_ready()
                {
                    return result_ && result_->has_value();
                }

                template <size_t... index>
                void await_suspend_impl(std::index_sequence<index...>, std::coroutine_handle<> continuation)
                {
                    auto promise_type = std::coroutine_handle<LazyPromiseBase>::from_address(
                                            continuation.address())
                                            .promise();
                    auto executor = promise_type.executor_;
                    auto input = std::move(input_);
                    // Make local copies to shared_ptr to avoid deleting objects too early
                    // if any coroutine finishes before this function.
                    auto result = std::make_shared<std::optional<ResultType>>();
                    auto event = std::make_shared<detail::CountEvent>(std::tuple_size<InputType>());

                    result_ = result;
                    (
                        [&]()
                        {
                            if (result->has_value())
                            {
                                return;
                            }
                            if (!std::get<index>(*input).coro_.promise().executor_)
                            {
                                std::get<index>(*input).coro_.promise().executor_ = executor;
                            }
                            std::get<index>(*input).start([r = result, c = continuation, e = event](
                                                              std::variant_alternative_t<index, ResultType> &&res) mutable
                                                          {
                                    assert(e != nullptr);
                                    auto count = e->downCount();
                                    if (count == std::tuple_size<InputType>() + 1) {
                                        *r = ResultType{std::in_place_index_t<index>(), std::move(res)};
                                        c.resume();
                                    } });
                        }(),
                        ...);
                }

                void await_suspend(std::coroutine_handle<> continuation)
                {
                    await_suspend_impl(std::make_index_sequence<sizeof...(Ts)>{}, std::move(continuation));
                }

                void await_resume()
                {
                    assert(result_ != nullptr);
                    return std::move(result_->value());
                }

                std::unique_ptr<std::tuple<LazyType<Ts>...>> input_;
                std::shared_ptr<std::optional<ResultType>> result_;
            };

            template <typename T, typename InAlloc, typename Callback = Unit>
            struct SimpleCollectAnyAwaitable
            {
                using ValueType = T;
                using LazyType = Lazy<T>;
                using VectorType = std::vector<LazyType, InAlloc>;

                VectorType input_;
                [[no_unique_address]] Callback callback_;

                SimpleCollectAnyAwaitable(std::vector<LazyType, InAlloc> &&input)
                    : input_(std::move(input)) {}

                SimpleCollectAnyAwaitable(std::vector<LazyType, InAlloc> &&input, Callback callback)
                    : input_(std::move(input)), callback_(std::move(callback)) {}

                auto coAwait(Executor *ex)
                {
                    if constexpr (std::is_same_v<Callback, Unit>)
                    {
                        return CollectAnyAwaiter<LazyType, InAlloc>(std::move(input_));
                    }
                    else
                    {
                        return CollectAnyAwaiter<LazyType, InAlloc, Callback>(std::move(input_), std::move(callback_));
                    }
                }
            };

            template <template <typename> typename LazyType, typename... Ts>
            struct SimpleCollectAnyVariadicAwaiter
            {
                using InputType = std::tuple<LazyType<Ts>...>;
                InputType inputs_;

                SimpleCollectAnyVariadicAwaiter(LazyType<Ts> &&...inputs)
                    : inputs_(std::move(inputs)...) {}

                auto coAwait(Executor *ex)
                {
                    return CollectAnyVariadicAwaiter(std::move(inputs_));
                }
            };

            template <typename Container, typename OAlloc, bool Para = false>
            struct CollectAllAwaiter
            {
                using ValueType = typename Container::value_type::ValueType;
                CollectAllAwaiter(Container &&input, OAlloc outAlloc)
                    : input_(std::move(input)), output_(outAlloc), event_(input_.size())
                {
                    output_.resize(input_.size());
                }

                CollectAllAwaiter(CollectAllAwaiter &&other) = delete;
                CollectAllAwaiter(const CollectAllAwaiter &) = delete;
                CollectAllAwaiter &operator=(cosnt CollectAllAwaiter &) = delete;

                inline bool await_ready() const noexcept
                {
                    return input_.empty();
                }

                inline void await_suspend(std::coroutine_handle<> continuation)
                {
                    auto promise_type = std::coroutine_handle<LazyPromiseBase>::from_address(continuation.address()).promise();
                    auto executor = promise_type.executor_;
                    for (size_t i = 0; i < input_.size(); ++i)
                    {
                        auto &exec = input_[i].coro_.promise().executor_;
                        if (exec == nullptr)
                        {
                            exec = executor;
                        }
                        auto &&func = [this, i]()
                        {
                            input_[i].start([this, i](Try<ValueType> &&result)
                                            {
                                output_[i] = std::move(result);
                                auto awaitingCoro = event_.down();
                                if(awaitingCoro) {
                                    awaitingCoro.resume();
                                } });
                        };
                        if (Para == true && input_.size() > 1)
                        {
                            if (exec != nullptr)
                            {
                                AS_LIKELY
                                {
                                    exec->schedule(func);
                                    continue;
                                }
                            }
                        }
                        func();
                        event_.setAwaitingCoro(continuation);
                        auto awaitingCoro = event_.down();
                        if (awaitingCoro)
                        {
                            awaitingCoro.resume();
                        }
                    }
                }

                inline auto await_resume()
                {
                    return std::move(output_);
                }

                Container input_;
                std::vector<Try<ValueType>, OAlloc> output_;
                detail::CountEvent event_;
            }; // CollectAllAwaiter

            template <typename Container, typename OAlloc, bool Para = false>
            struct SimpleCollectAllAwaitable
            {
                Container input_;
                OAlloc out_alloc_;
                SimpleCollectAllAwaitable(Container &&input, OAlloc out_alloc)
                    : input_(std::move(input)), out_alloc_(out_alloc) {}

                auto coAwait(Executor *ex)
                {
                    return CollectAllAwaiter<Container, OAlloc, Para>(std::move(input_), out_alloc_);
                }
            };
        } // namespace detail

        namespace detail
        {
            template <typename T>
            struct is_lazy : std::false_type
            {
            };

            template <typename T>
            struct is_lazy<Lazy<T>> : std::true_type
            {
            };

            template <bool Para, typename Container, typename T = typename Container::value_type::ValueType,
                      typename OAlloc = std::allocator<Try<T>>>
            inline auto collectAllImpl(Container input, OAlloc out_alloc = OAlloc())
            {
                using LazyType = typename Container::value_type;
                using AT = std::conditional_t<is_lazy<LazyType>::value,
                                              detail::SimpleCollectAllAwaitable<Container, OAlloc, Para>,
                                              detail::CollectAllAwaiter<Container, OAlloc, Para>>;
                return AT(std::move(input), out_alloc);
            }

            template <bool Para, typename Container,
                      typename T = typename Container::value_type::ValueType,
                      typename OAlloc = std::allocator<Try<T>>>
            inline auto collectAllWindowedImpl(size_t maxConcurrency, bool yield, /*yield between two batchs*/
                                               Container input, OAlloc out_alloc = OAlloc()) -> Lazy<std::vector<Try<T>, OAlloc>>
            {
                using LazyType = typename Container::value_type;
                using AT = std::conditional_t<is_lazy<LazyType>::value,
                                              detail::SimpleCollectAllAwaitable<Container, OAlloc, Para>,
                                              detail::CollectAllAwaiter<Container, OAlloc, Para>>;
                std::vector<Try<T>, OAlloc> output(out_alloc);
                output.reserve(input.size());
                size_t input_size = input.size();
                // maxConcurrent == 0;
                // input_size <= maxConcurrency size;
                // act just like CollectAll.
                if (maxConcurrency == 0 || input_size <= maxConcurrency)
                {
                    co_return co_await AT(std::move(input), out_alloc);
                }
                size_t start = 0;
                while (start < input_size)
                {
                    size_t end = (std::min)(input_size, start + maxConcurrency);
                    std::vector<LazyType> tmp_group(std::make_move_iterator(input.begin() + start),
                                                    std::make_move_iterator(input.begin() + end));
                    start = end;
                    for (auto &t : co_await AT(std::move(tmp_group), out_alloc))
                    {
                        output.push_back(std::move(t));
                    }
                    if (yield)
                    {
                        co_await Yield{};
                    }
                }
                co_return std::move(output);
            }

            // variadic collectAll
            template <bool Para, template <typename> typename LazyType, typename... Ts>
            struct CollectAllVariadicAwaiter
            {
                using ResultType = std::tuple<Try<Ts>...>;
                using InputType = std::tuple<LazyType<Ts>...>;

                CollectAllVariadicAwaiter(LazyType<Ts> &&...inputs)
                    : inputs_(std::move(inputs)...), event_(sizeof...(Ts)) {}

                CollectAllVariadicAwaiter(InputType &&inputs)
                    : inputs_(std::move(inputs)), event_(sizeof...(Ts)) {}

                CollectAllVariadicAwaiter(const CollectAllVariadicAwaiter &) = delete;
                CollectAllVariadicAwaiter &operator=(const CollectAllVariadicAwaiter &) = delete;
                CollectAllVariadicAwaiter(CollectAllVariadicAwaiter &&) = default;

                bool await_ready() const noexcept
                {
                    return false;
                }

                template <size_t... index>
                void await_suspend_impl(std::index_sequence<index...>, std::coroutine_handle<> continuation)
                {
                    auto promise_type = std::coroutine_handle<LazyPromiseBase>::from_address(continuation.address()).promise();
                    auto executor = promise_type.executor_;
                    event_.setAwaitingCoro(continuation);
                    // fold expression
                    (
                        [executor, this](auto &lazy, auto &result)
                        {
                            auto &&exec = lazy.coro_.promise().executor_;
                            if (exec == nullptr)
                            {
                                exec = executor;
                            }
                            auto func = [&]()
                            {
                                lazy.start([&](auto &&res)
                                           {
                                    result = std::move(res);
                                    if (auto awaitingCoro = event_.down(); awaitingCoro) {
                                        awaitingCoro.resume();
                                    } });
                            };
                            if constexpr (Para == true && sizeof...(Ts) > 1)
                            {
                                if (exec != nullptr)
                                    AS_LIKELY { exec->schedule(std::move(func)); }
                                else
                                    AS_UNLIKELY { func(); }
                            }
                            else
                            {
                                func();
                            }
                        }(std::get<index>(inputs_), std::get<index>(results_)),
                        ...)
                }

                void await_suspend(std::coroutine_handle<> continuation)
                {
                    await_suspend_impl(std::make_index_sequence<sizeof...(Ts)>{}, std::move(continuation));
                }

                auto await_resume()
                {
                    return std::move(results_);
                }

                InputType inputs_;
                ResultType results_;
                detail::CountEvent event_;
            };

            template <bool Para, template <typename> typename LazyType, typename... Ts>
            struct SimpleCollectAllVariadicAwaiter
            {
                using InputType = std::tuple<LazyType<Ts>...>;

                SimpleCollectAllVariadicAwaiter(LazyType<Ts> &&...inputs)
                    : inputs_(std::move(inputs)...) {}

                auto coAwait(Executor *ex)
                {
                    return CollectAllVariadicAwaiter<Para, LazyType, Ts...>(std::move(inputs_));
                }
                InputType inputs_;
            };

            template <bool Para, template <typename> typename LazyType, typename... Ts>
            inline auto collectAllVariadicImpl(LazyType<Ts> &&...awaitables)
            {
                static_assert(sizeof...(Ts) > 0);
                using AT = std::conditional_t<is_lazy<LazyType<void>>::value, SimpleCollectAllVariadicAwaiter<Para, LazyType, Ts...>,
                                              CollectAllVariadicAwaiter<Para, LazyType, Ts...>>;
                return AT(std::move(awaitables)...);
            }

            // collectAny
            template <typename T, template <typename> typename LazyType,
                      typename IAlloc = std::allocator<LazyType<T>>,
                      typename Callback = Unit>
            inline auto collectAnyImpl(std::vector<LazyType<T>, IAlloc> input,
                                       Callback callback = {})
            {
                using AT = std::conditional_t<std::is_same_v<LazyType<T>, Lazy<T>>,
                                              detail::SimpleCollectAnyAwaitable<T, IAlloc, Callback>,
                                              detail::CollectAnyAwaiter<LazyType<T>, IAlloc, Callback>>;
                return AT(std::move(input), std::move(callback));
            }

            template <template <typename> typename LazyType, typename... Ts>
            inline auto CollectAnyVariadicImpl(LazyType<Ts> &&...inputs)
            {
                using AT = std::conditional_t<is_lazy<LazyType<void>>::value,
                                              SimpleCollectAnyVariadicAwaiter<LazyType, Ts...>,
                                              CollectAnyVariadicAwaiter<LazyType, Ts...>>;
                return AT(std::move(inputs)...);
            }

            // collectAnyVariadicPair
            template <typename T, typename... Ts>
            inline auto CollectAnyVariadicPairImpl(T &&input, Ts &&...inputs)
            {
                using U = std::tuple_element_t<0, std::remove_cvref_t<T>>;
                using AT = std::conditional_t<is_lazy<U>::value, SimpleCollectAnyVariadicPairAwaiter<T, Ts...>,
                                              CollectAnyVariadicPairAwaiter<T, Ts...>>;
                return AT(std::move(input), std::move(inputs)...);
            }
        } // namespace detail

        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>>
        inline auto collectAny(std::vector<LazyType<T>, IAlloc> &&input)
        {
            return detail::collectAnyImpl(std::move(input));
        }

        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>, typename Callback>
        inline auto collectAny(std::vector<LazyType<T>, IAlloc> &&input, Callback callback)
        {
            auto cb = std::make_shared<Callback>(std::move(callback));
            return detail::collectAnyImpl(std::move(input), std::move(cb));
        }

        template <template <typename> typename LazyType, typename... Ts>
        inline auto collectAny(LazyType<Ts>... awaitables)
        {
            static_assert(sizeof...(Ts), "collectAny need at least one param!");
            return detail::CollectAnyVariadicImpl(std::move(awaitables)...);
        }

        // collectAny with std::pair<Lazy, CallbackFunction>
        template <typename... Ts>
        inline auto collectAny(Ts &&...inputs)
        {
            static_assert(sizeof...(Ts), "collectAny need at least one param!");
            return detail::CollectAnyVariadicPairImpl(std::move(inputs)...);
        }

        // The collectAll() function can be used to co_await on a vector of LazyType
        // tasks in **one thread**,and producing a vector of Try values containing each
        // of the results.
        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>,
                  typename OAlloc = std::allocator<Try<T>>>
        inline auto collectAll(std::vector<LazyType<T>, IAlloc> &&input, OAlloc out_alloc = OAlloc())
        {
            return detail::collectAllImpl<false>(std::move(input), out_alloc);
        }

        // Like the collectAll() function above, The collectAllPara() function can be
        // used to concurrently co_await on a vector LazyType tasks in executor,and
        // producing a vector of Try values containing each of the results.
        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>,
                  typename OAlloc = std::allocator<Try<T>>>
        inline auto collectAllPara(std::vector<LazyType<T>, IAlloc> &&input, OAlloc out_alloc = OAlloc())
        {
            return detail::collectAllImpl<true>(std::move(input), out_alloc);
        }

        // This collectAll function can be used to co_await on some different kinds of
        // LazyType tasks in one thread,and producing a tuple of Try values containing
        // each of the results.
        template <template <typename> typename LazyType, typename... Ts>
        // The temporary object's life-time which binding to reference(inputs) won't
        // be extended to next time of coroutine resume. Just Copy inputs to avoid
        // crash.
        inline auto collectAll(LazyType<Ts>... inputs)
        {
            static_assert(sizeof...(Ts), "collectAll need at least one param!");
            return detail::collectAllVariadicImpl<false>(std::move(inputs)...);
        }

        // Like the collectAll() function above, This collectAllPara() function can be
        // used to concurrently co_await on some different kinds of LazyType tasks in
        // executor,and producing a tuple of Try values containing each of the results.
        template <template <typename> typename LazyType, typename... Ts>
        inline auto collectAllPara(LazyType<Ts>... inputs)
        {
            static_assert(sizeof...(Ts), "collectAllPara need at least one param!");
            return detail::collectAllVariadicImpl<true>(std::move(inputs)...);
        }

        // Await each of the input LazyType tasks in the vector, allowing at most
        // 'maxConcurrency' of these input tasks to be awaited in one thread. yield is
        // true: yield collectAllWindowedPara from thread when a 'maxConcurrency' of
        // tasks is done.
        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>,
                  typename OAlloc = std::allocator<Try<T>>>
        inline auto collectAllWindowed(size_t maxCOncurrency, bool yield /*yield between two batchs*/,
                                       std::vector<LazyType<T>, IAlloc> &&input, OAlloc out_alloc = OAlloc())
        {
            return detail::collectAllWindowedImpl<true>(maxCOncurrency, yield, std::move(input), out_alloc);
        }

        // Await each of the input LazyType tasks in the vector, allowing at most
        // 'maxConcurrency' of these input tasks to be concurrently awaited at any one
        // point in time.
        // yield is true: yield collectAllWindowedPara from thread when a
        // 'maxConcurrency' of tasks is done.
        template <typename T, template <typename> typename LazyType,
                  typename IAlloc = std::allocator<LazyType<T>>,
                  typename OAlloc = std::allocator<Try<T>>>
        inline auto collectAllWIndowedPara(size_t maxConcurrency, bool yield /*yield between two batchs*/,
                                           std::vector<LazyType<T>, IAlloc> &&input, OAlloc out_alloc = OAlloc())
        {
            return detail::collectAllWindowedImpl<false>(maxConcurrency, yield, std::move(input), out_alloc);
        }

    } // namespace coro
} // namespace async_framework
