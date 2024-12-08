/* A simple condition implementation
*/

#ifndef ASYNC_FRAMEWORK_CONDITION_H
#define ASYNC_FRAMEWORK_CONDITION_H

#include <semaphore>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace async_framework::util
{
    class Condition : public std::binary_semaphore
    {
    public:
        explicit Condition(ptrdiff_t num = 0) : std::binary_semaphore(num) {}
    };
}

#endif