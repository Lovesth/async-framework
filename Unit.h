#ifndef ASYNC_FRAMEWORK_UNIT_H
#define ASYNC_FRAMEWORK_UNIT_H
namespace async_framework
{
    // 作为最基本的类型使用
    // 所有Unit都相等
    struct Unit
    {
        constexpr bool operator==(const Unit &) const
        {
            return true;
        }
        constexpr bool operator!=(const Unit &) const
        {
            return false;
        }
    };
} // namespace async_framework

#endif