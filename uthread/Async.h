#pragma once
#include <memory>
#include <type_traits>
#include <tuple>
#include "Uthread.h"

namespace async_framework
{
    namespace uthread
    {
        enum class Launch
        {
            Prompt,
            Schedule,
            Current,
        };
        // Async: 将f和ex交给协程去执行
        template <Launch policy, typename F>
            requires(policy == Launch::Prompt)
        inline Uthread async(F &&f, Executor *ex)
        {
            return Uthread(Attribute{ex}, std::forward<F>(f));
        }

        template <Launch policy, class F>
            requires(policy == Launch::Schedule)
        inline void async(F &&f, Executor *ex)
        {
            if (!ex)
                AS_UNLIKELY { return; }
            ex->schedule([f = std::move(f), ex]()
                         { Uthread uth(Attribute{ex}, std::move(f)); });
        }

        // schedule async task, set a callback
        template <Launch policy, typename F, typename C>
            requires(policy == Launch::Schedule)
        inline void async(F &&f, C &&c, Executor *ex)
        {
            if (!ex)
                AS_UNLIKELY { return; }
            ex->schedule([f = std::move(f), c = std::move(c), ex]()
                         {
                Uthread uth(Attribute{ex}, std::move(f));
                uth.join(std::move(c)); });
        }

        template <Launch policy, typename F>
            requires(policy == Launch::Current)
        inline void async(F &&f, Executor *ex)
        {
            Uthread uth(Attribute{ex}, std::forward<F>(f));
            uth.detach();
        }

        template <typename F, typename... Args, typename R = std::invoke_result_t<F &&, Args &&...>>
        inline Future<R> async(Launch policy, Attribute attr, F &&f, Args &&...args)
        {
            if (policy == Launch::Schedule)
            {
                if (!attr.ex)
                    AS_UNLIKELY
                    {
                        assert(false);
                    }
            }
            Promise<R> p;
            auto rc = p.getFuture().via(attr.ex);
            auto proc = [p = std::move(p), ex = attr.ex, f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable
            {
                if (ex)
                {
                    p.forceSched().checkout();
                }
                if constexpr (std::is_void_v<R>)
                {
                    std::apply(f, std::move(args));
                    p.setValue();
                }
                else
                {
                    p.setValue(std::apply(f, std::move(args)));
                }
            };
            if (policy == Launch::Schedule)
            {
                attr.ex->schedule([fn = std::move(proc), attr]()
                                  { Uthread(attr, std::move(fn)).detach(); });
            }
            else if (policy == Launch::Current)
            {
                Uthread(attr, std::move(proc)).detach();
            }
            else
            {
                // TODO log
                assert(false);
            }
            return rc;
        }

    } // namespace uthread

} // namespace async_framework