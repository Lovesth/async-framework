#pragma once
#include <type_traits>
#include "../Future.h"
#include "../Promise.h"
#include "Await.h"

namespace async_framework
{
    namespace uthread
    {
        // The semantics of uthread::Latch is similar to std::latch. The design of
        // uthread::Latch also mimics the design of std::latch. The uthread::Latch is
        // used to synchronize different uthread stackful coroutine. The uthread
        // stackful coroutine who is awaiting a uthread::Latch would be suspended until
        // the counter in the awaited uthread::Latch counted to zero.
        //
        // Example:
        //
        // ```C++
        // Latch latch(2);
        // latch.await();
        //      // In another uthread
        //      latch.downCount();
        //      // In another uthread
        //      latch.downCount();
        // ```

        class Latch
        {
        public:
            explicit Latch(std::size_t count) : count_(count), skip_(!count)
            {
            }

            Latch(const Latch &) = delete;
            Latch(Latch &&) = delete;

            ~Latch() {}

        public:
            void downCount(std::size_t n = 1)
            {
                if (skip_)
                {
                    return;
                }
                auto lastCount = count_.fetch_sub(n, std::memory_order_acq_rel);
                if (lastCount == 1u)
                {
                    promise_.setValue(true);
                }
            }

            void await(Executor *ex)
            {
                if (skip_)
                {
                    return;
                }
                uthread::await(promise_.getFuture().via(ex));
            }

            std::size_t currentCount() const
            {
                return count_.load(std::memory_order_acquire);
            }

        private:
            Promise<bool> promise_;
            std::atomic<std::size_t> count_;
            bool skip_;
        };
    } // namespace uthread
} // namespace async_framework