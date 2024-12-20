#pragma once
#include <atomic>

#include "../../util/concurrentqueue.h"

namespace coro_io::detail
{
    using namespace apps::detail;
    template <typename client_t>
    class client_queue
    {
        moodycamel::ConcurrentQueue<client_t> queue_[2];
        std::atomic_int_fast16_t selected_index_ = 0;
        std::atomic<std::size_t> size_[2] = {};

    public:
        std::atomic<std::size_t> collect_cnt_ = 0;

    private:
        struct fake_client
        {
            template <typename T>
            fake_client &operator=(T &&) noexcept
            {
                return *this;
            }
        };
        struct fake_iter
        {
            fake_iter operator++()
            {
                return *this;
            }
            fake_iter operator++(int)
            {
                return *this;
            }
            fake_client &operator*()
            {
                static fake_client c;
                return c;
            }
        };

    public:
        client_queue(std::size_t reserve_size = 0)
            : queue_{moodycamel::ConcurrentQueue<client_t>(reserve_size), moodycamel::ConcurrentQueue<client_t>(reserve_size)}
        {
        }

        std::size_t size() const noexcept
        {
            return size_[0] + size_[1];
        }

        void reselect()
        {
            selected_index_ ^= 1;
        }

        std::size_t enqueue(client_t &&c)
        {
            const int_fast16_t index = selected_index_;
            auto cnt = ++size_[index];
            if (queue_[index].enqueue(std::move(c)))
            {
                return cnt;
            }
            else
            {
                --size_[index];
                return 0;
            }
        }

        bool try_dequeue(client_t &c)
        {
            const int_fast16_t index = selected_index_;
            if (size_[index ^ 1])
            {
                if (queue_[index ^ 1].try_dequeue(c))
                {
                    --size_[index ^ 1];
                    return true;
                }
            }
            if (queue_[index].try_dequeue(c))
            {
                --size_[index];
                return true;
            }
            return false;
        }

        std::size_t clear_old(std::size_t max_clear_cnt)
        {
            const int_fast16_t index = selected_index_ ^ 1;
            if (size_[index])
            {
                std::size_t result = queue_[index].try_dequeue_bulk(fake_iter, max_clear_cnt);
                size_[index] -= result;
                return result;
            }
            return 0;
        }
    };
} // namespace coro_io::detail