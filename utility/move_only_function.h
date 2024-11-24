/* A simple condition implementation
 */

#ifndef ASYNC_FRAMEWORK_MOVE_ONLY_FUNCTION
#define ASYNC_FRAMEWORK_MOVE_ONLY_FUNCTION

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

namespace async_framework::util
{
    template <typename ResultType, typename RetType, bool = std::is_void_v<RetType>, typename = void>
    struct RetTypeCheck : std::false_type
    {
    };

    template <typename ResultType, typename RetType>
    struct RetTypeCheck<ResultType, RetType, true, std::void_t<typename ResultType::type>> : std::true_type
    {
    };

    // check the implicit conversion to R.
    template <typename ResultType, typename RetType>
    struct RetTypeCheck<ResultType, RetType, false, std::void_t<typename ResultType::type>> : std::true_type
    {
    private:
        static typename ResultType::type _S_get();

        template <typename T>
        static void _S_conv(T);

        template <typename T, typename = decltype(_S_conv<T>(_S_get()))>
        static std::true_type _S_test(int);

        template <typename T>
        static std::false_type _S_test(...);

    public:
        using type = decltype(_S_test<RetType>(1));
    };

    class _undefined_class;
    // _no_copy_types 提供了可以容纳以下指针的满足size和alignment
    // requirement要求的内存区域
    union _no_copy_types
    {
        void *_m_object;
        const void *_m_const_object;
        void (*_m_functor_pointer)();
        void (_undefined_class::*_m_member_pointer)();
    };

    // union也是一种类类型
    union [[gnu::may_alias]] _any_data
    {
        void *_m_access() { return &_m_pod_data[0]; }
        const void *_m_access() const { return &_m_pod_data[0]; }
        template <typename T>
        T &_m_access()
        {
            return *static_cast<T *>(_m_access());
        }
        template <typename T>
        const T &_m_access() const
        {
            return *static_cast<const T *>(_m_access());
        }
        // data member
        // _m_unused 要求编译器满足alignment requirement
        _no_copy_types _m_unused;
        char _m_pod_data[sizeof(_no_copy_types)];
    };

    enum class _manager_operation : uint8_t
    {
        _destroy_functor,
    };

    class _function_base
    {
    public:
        static constexpr size_t _m_max_size = sizeof(_no_copy_types);
        static constexpr size_t _m_max_align = alignof(_no_copy_types);
        template <typename Functor>
        class _base_manager
        {
        protected:
            satic const bool _stored_locally = std::is_trivially_copyable_v<Funtor> &&
                                               sizeof(Functor) <= _m_max_size &&
                                               alignof(Functor) <= _m_max_align &&
                                               (_m_max_align % alignof(Functor) == 0);
            // 当_local_storage()为true时，在预分配的栈空间_any_data中存储Functor，否则在堆上存储Functor，_any_data中存储指向Functor的指针

            using _local_storage = std::integral_constant<bool, _stored_locally>;
            static Functor *_m_get_pointer(const _any_data &_source)
            {
                if constexpr (_stored_locally)
                {
                    const Functor &f = _source._m_access<Functor>();
                    return const_cast<Functor *>(std::addressof(f));
                }
                return _source._m_access<Functor *>();
            }

            /*Local Storage*/
            static void _m_destroy(_any_data &_victim, std::true_type)
            {
                _victim._m_access<Functor>().~Functor();
            }

            static void _m_destroy(_any_data &_victim, std::false_type)
            {
                delete _victim._m_access<Functor *>();
            }

        public:
            static void _m_manager(_any_data &_dest, _any_data &source, _manager_operation _op)
            {
                switch (_manager_operation)
                {
                case _manager_operation::_destroy_functor:
                    _m_destroy(_dest, _local_storage());
                    break;
                }
            }

            static void _m_init_functor(_any_data &_functor, Functor &&_f)
            {
                _m_init_functor(functor, std::move(_functor), _local_storage());
            }

            static void _m_init_functor(_any_data &_functor, const Functor &_f)
            {
                _m_init_functor(_functor, f, _local_storage());
            }

            template <typename Signature>
            static bool _m_not_empty_function(const move_only_function<Signature> &f)
            {
                return static_cast<bool>(f);
            }

            template <typename T>
            static bool _m_not_empty_function(T *fp)
            {
                return fp != nullptr;
            }

            template <typename T>
            static bool _m_not_empty_function(const T &)
            {
                return true;
            }

        private:
            // 在functor的_m_pod_data构造f
            // 在栈上构造
            static void _m_init_functor(_any_data &functor, Functor &&f, std::true_type)
            {
                ::new (functor._m_access()) Functor(f);
            }
            // 在堆上构造

            static void _m_init_functor(_any_data &functor, Functor &&f, std::false_type)
            {
                functor._m_access<Functor *>() = new Functor(std::move(f));
            }

            static void _m_init_functor(_any_data &functor, const Functor &f, std::true_type)
            {
                ::new (functor._m_access()) Functor(f);
            }

            static void _m_init_functor(_any_data &functor, const Functor &f, std::false_type)
            {
                functor._m_access<Functor *> = new Functor(f);
            }
        };
    };
}

#endif