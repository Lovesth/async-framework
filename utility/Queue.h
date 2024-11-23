#ifndef ASYNC_FRAMEWORK_QUEUE_H
#define ASYNC_FRAMEWORK_QUEUE_H

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>
#include <functional>

namespace async_framework::util
{
    template <typename T>
        requires std::is_move_assignable_v<T>
    class Queue
    {
    public:
        void push(T &&elem)
        {
            {
                std::scoped_lock guard(mutex_);
                queue_.push(std::move(T));
            }
            cond_.notify_one();
        }
        bool try_push(const T &elem)
        {
            {
                std::unique_lock lock(mutex_, std::try_to_lock);
                if (!lock)
                    return false;
                queue_.push(elem);
            }
            cond_.notify_one();
            return true;
        }
        bool pop(T &elem)
        {
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [&]()
                       { return !queue_.empty() || stop_ });
            if (queue_.empty())
                return false;
            item = std::move(queue_.front());
            queue_.pop();
            return true;
        }
        bool try_pop(T &elem)
        {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock || queue_.empty())
                return false;
            elem = move(queue_.front());
            queue_.pop();
            return true;
        }
        bool try_pop_if(T &elem, function<bool(T &)> predict = nullptr)
        {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (!lock || queue_.empty())
                return false;
            if (predict && !predict(queue_.front()))
                return false;
            elem = std::move(queue_.front());
            queue_.pop();
            return true;
        }
        std::size_t size() const
        {
            std::scoped_lock lock(mutex_);
            return queue_.size();
        }
        bool empty()
        {
            std::scoped_lock lock(mutex_);
            return queue_.empty();
        }
        bool stop()
        {
            {
                std::scoped_lock lock(mutex_);
                stop_ = true;
            }
            cond_.notify_all();
        }

    private:
        std::queue<T> queue_;
        bool stop_ = false;
        mutable std::mutex mutex_;
        std::condition_variable cond_;
    };
}

#endif