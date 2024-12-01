/* A simple thread pool implementation
*/
#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>
#include <format>
#include "../util/Queue.h"

namespace async_framework::util
{
    class ThreadPool
    {
    public:
        struct WorkItem
        {
            // 是否允许偷取策略
            bool canSteal = false;
            std::function<void()> fn = nullptr;
        };

        enum class ERROR_TYPE
        {
            ERROR_NONE = 0,
            ERROR_POOL_HAS_STOP,
            ERROR_POOL_ITEM_IS_NULL,
        };
        explicit ThreadPool(size_t threadNum = std::thread::hardware_concurrency(), bool enableWorkSteal = false, bool enableCoreBindings = false);
        ~ThreadPool();
        ThreadPool::ERROR_TYPE scheduleById(std::function<void()> fn, int32_t id = -1);
        int32_t getCurrentId() const;
        size_t getItemCount() const;
        int32_t getThreadNum() const
        {
            return threadNum_;
        }

    private:
        std::pair<size_t, ThreadPool *> *getCurrent() const;
        int32_t threadNum_;
        std::vector<Queue<WorkItem>> queues_;
        std::vector<std::thread> threads_;
        std::atomic<bool> stop_;
        bool enableWorkSteal_;
        bool enableCoreBindings_;
    };
#ifdef __linux__
    // 获取当前进程允许使用的cpu id
    inline void getCurrentCpus(std::vector<uint32_t> &ids)
    {
        cpu_set_t set_;
        ids.clear();
        if (sched_getaffinity(0, sizeof(set_), &set_) == 0)
        {
            for (int i = 0; i < CPU_SETSIZE; i++)
            {
                if (CPU_ISSET(i, &set_))
                    ids.emplace_back(i);
            }
        }
    }
#endif
    inline ThreadPool::ThreadPool(size_t threadNum, bool enableWorkSteal, bool enableCoreBindings)
        : threadNum_(threadNum), enableWorkSteal_(enableWorkSteal), enableCoreBindings_(enableCoreBindings),
          queues_(threadNum_), stop_(false)
    {
        auto worker = [this](size_t id)
        {
            auto current = getCurrent();
            current->first = id;
            current->second = this;
            while (true)
            {
                WorkItem workerItem{};
                if (enableWorkSteal_)
                {
                    // 当前线程从其它任务队列偷取任务
                    for (int i = 0; i < threadNum_ * 2; i++)
                    {
                        if (queues_[(i + id) % threadNum_].try_pop_if(workerItem, [](auto &&elem)
                                                                      { return elem.canSteal }))
                            break;
                    }
                }
                if (!workerItem.fn && !queues_[id].pop(workerItem))
                {
                    if (stop_)
                    {
                        break;
                    }
                    else
                    {
                        continue;
                    }
                }
                if (workerItem.fn)
                    workerItem.fn();
            }
        };
        threads_.reserve(threadNum_);
#ifdef __linux__
        std::vector<uint32_t> cpu_ids;
        // 获取当前进程可用的cpuids
        if (enableCoreBindings_)
            getCurrentCpus(cpu_ids);
#else
        (void)enableCoreBindings_;
#endif

        // 启动threadNum_个线程
        for (auto i = 0; i < threadNum_; i++)
        {
            threads_.emplace_back(worker, i);
#ifdef __linux__
            if (!enableCoreBindings_)
                continue;
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu_ids[i % cpu_ids.size()], &cpuset);
            int res = sched_setaffinity(threads_[i].native_handle(), sizeof(cpu_set_t), &cpuset);
            if (res != 0)
                std::cerr << std::format("Error while calling sched_setaffinity: {}\n", res);
#endif
        }
    }

    ThreadPool::~ThreadPool()
    {
        stop_ = true;
        for (auto &queue : queues_)
            queue.stop();
        for (auto &thread : threads_)
            thread.join();
    }

    inline ThreadPool::ERROR_TYPE ThreadPool::scheduleById(std::function<void()> fn, int32_t id)
    {
        using ERROR_TYPE = ThreadPool::ERROR_TYPE;
        if (fn == nullptr)
        {
            return ERROR_TYPE::ERROR_POOL_ITEM_IS_NULL;
        }
        if (stop_)
        {
            return ERROR_TYPE::ERROR_POOL_HAS_STOP;
        }
        WorkItem workerItem{true, fn};
        if (id == -1)
        {
            if (enableWorkSteal_)
            {
                for (int i = 0; i < threadNum_ * 2; i++)
                {
                    if (queues_[i % threadNum_].try_push(workerItem))
                        return ERROR_TYPE::ERROR_NONE;
                }
            }
            id = std::rand() % threadNum_;
            queues_[id].push(std::move(workerItem));
        }
        else
        {
            assert(id < threadNum_);
            queues_[id].push(std::move(workerItem));
        }
        return ERROR_TYPE::ERROR_NONE;
    }

    std::pair<size_t, ThreadPool *> *ThreadPool::getCurrent() const
    {
        static thread_local std::pair<size_t, ThreadPool *> current(-1, nullptr);
        return &current;
    }

    inline int32_t ThreadPool::getCurrentId() const {
        auto current = getCurrent();
        if(this==current->second){
            return current->first;
        }
        return -1;
    }

    inline size_t ThreadPool::getItemCount() const {
        size_t res = 0;
        for(auto& queue : queues_){
            res += queue.size();
        }
        return res;
    }
}