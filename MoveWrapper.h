#ifndef ASYNC_FRAMEWORK_MOVEWRAPPER_H
#define ASYNC_FRAMEWORK_MOVEWRAPPER_H

#include <utility>

namespace async_framework
{
    template <typename T>
    class [[deprecated]] MoveWrapper
    {
    public:
        MoveWrapper() = default;
        MoveWrapper(T &&value) : value_(std::move(value)) {}
        MoveWrapper(const MoveWrapper &other) : value_(move(other.value_)) {}
        MoveWrapper(MoveWrapper &&other) : value_(std::move(other.value_)) {}

        // 禁止拷贝和移动对象
        MoveWrapper &operator=(const MoveWrapper &) = delete;
        MoveWrapper &operator=(MoveWrapper &&) = delete;

        T &get()
        {
            return value_;
        }

        const T &get() const
        {
            return value_;
        }

    private:
        mutable T value_;
    };
} // namespace async_framework

#endif