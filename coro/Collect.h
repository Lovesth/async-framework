#pragma once

#include <array>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
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
            : idx_(idx), value_(std::move(value))
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

        bool hasError() conse { return value_.hasError(); }

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
        CollectAnyVariadicAwaiter(Ts &&...inputs)
            : input_(std::move(inputs)...), result_(nullptr) {}

        CollectAnyVariadicAwaiter(InputType &&inputs)
            : input_(std::move(inputs)), result_(nullptr) {}

        CollectAnyVariadicAwaiter(const CollectAnyVariadicAwaiter &) = delete;

        CollectAnyVariadicAwaiter &operator=(const CollectAnyVariadicAwaiter &) =
            delete;

        CollectAnyVariadicAwaiter(CollectAnyVariadicAwaiter &&other)
            : input_(std::move(other.input_)), result_(std::move(other.result_)) {}

        bool await_ready() const noexcept { return result_ && result_->has_value(); }

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
    } // namespace detail
  } // namespace coro
} // namespace async_framework
