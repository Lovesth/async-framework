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
    };
} // namespace coro_io::detail