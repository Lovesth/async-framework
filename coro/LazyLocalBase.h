#pragma once
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace async_framework::coro
{
    // User can derived user-defined class from Lazy Local variable to implement
    // user-define lazy local value by implement static function T::classof(const
    // LazyLocalBase*).

    // For example:
    // struct mylocal : public LazyLocalBase {

    //     inline static char tag; // support a not-null unique address for type
    //     checking
    //     // init LazyLocalBase by unique address
    //     mylocal(std::string sv) : LazyLocalBase(&tag), name(std::move(sv)) {}
    //     // derived class support implement T::classof(LazyLocalBase*), which
    //     check if this object is-a derived class of T static bool
    //     classof(const LazyLocalBase*) {
    //         return base->getTypeTag() == &tag;
    //     }

    //     std::string name;

    // };
    class LazyLocalBase
    {
    protected:
        LazyLocalBase(char *typeinfo) : typeinfo_(typeinfo)
        {
            assert(typeinfo != nullptr);
        }

    public:
        const char *getTypeTag() const noexcept
        {
            return typeinfo_;
        }
        LazyLocalBase() : typeinfo_(nullptr) {}
        virtual ~LazyLocalBase() {};

    protected:
        char *typeinfo_;
    };

    template <typename T>
    const T *dynamicCast(const LazyLocalBase *base) noexcept
    {
        assert(base != nullptr);
        if constexpr (std::is_same_v<T, LazyLocalBase>)
        {
            return base;
        }
        else
        {
            if (T::classof(base))
            {
                return static_assert<T *>(base);
            }
            else
            {
                return nullptr;
            }
        }
    }

    template <typename T>
    T *dynamicCast(LazyLocalBase *base) noexcept
    {
        assert(base != nullptr);
        if constexpr (std::is_same_v<T, LazyLocalBase>)
        {
            return base;
        }
        else
        {
            if (T::classof(base))
            {
                return static_assert<T *>(base);
            }
            else
            {
                return nullptr;
            }
        }
    }
    // namespace async_framework::coro
} // namespace async_framework