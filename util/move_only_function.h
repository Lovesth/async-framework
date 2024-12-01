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
    template <typename Signature>
    class move_only_function;

    namespace detail
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
            // 用于判断RetType是否和ResultType::type一样
            using type = decltype(_S_test<RetType>(1));
        };

        class _undefined_class;
        // _no_copy_types 提供了可以容纳以下指针指向的、满足size和alignment
        // requirement要求的内存区域
        union _no_copy_types
        {
            void *_m_object;
            const void *_m_const_object;
            void (*_m_functor_pointer)();
            void (_undefined_class::*_m_member_pointer)();
        };

        // union也是一种类类型
        // _any_data至少是8Bytes(64位)，4Bytes(32位)，为了实现对齐，有可能会更大。
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

                // 实现非空函数判断：1、move_only_function, 2、函数指针，3、其它类型。
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
                // _any_data为一个指针的大小，如果f不大于一个指针大小，存储在栈上
                // 否则_any_data存储指向f的指针
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

            _function_base() : _m_manager(nullptr) {}
            ~_function_base()
            {
                if (_m_manager)
                {
                    _m_manager(_m_functor, _m_functor, _manager_operation::_destroy_functor);
                }
            }
            bool _m_empty() const { return !_m_manager; }

            using manger_type = void (*)(_any_data &, const _any_data &, _manager_operation);
            _any_data _m_functor;
            manger_type _m_manager;
        };

        template <typename Signature, typename Functor>
        class FunctionHandler;

        template <typename Res, typename Functor, typename... ArgTypes>
        class FunctionHandler<Res(ArgTypes...), Functor> : public _function_base::_base_manager<Functor>
        {
            using BaseType = _function_base::_base_manager<Functor>;

        public:
            static void _m_manager(_any_data &_dest, const _any_data &_source, _manager_operation _op)
            {
                return BaseType::_m_manager(_dest, _source, _op);
            }

            static Res _m_invoke(const _any_data &_functor, ArgTypes &&..._args)
            {
                if constexpr (std::is_same_v<Res, void>)
                {
                    std::invoke(*BaseType::_m_get_pointer(_functor), std::forward<ArgTypes>(_args)...);
                }
                else
                {
                    return std::invoke(*BaseType::_m_get_pointer(_functor), std::forward<ArgTypes>(_args)...);
                }
            }
        };

        template <>
        class FunctionHandler<void, void>
        {
        public:
            static void _m_manager(_any_data &, const _any_data &, _manager_operation)
            {
                return;
            }
        };
    } // namespace detail

    template <typename RetType, typename... ArgTypes>
    class move_only_function<RetType(ArgTypes...)> : private detail::_function_base
    {
        template <typename Func, typename Res2 = std::invoke_result<Func, ArgTypes...>>
        struct NotMoveOnlyCallable : public detail::RetTypeCheck<Res2, RetType>::type
        {
        };

        template <typename T>
        struct NotMoveOnlyCallable<move_only_function, T> : public std::false_type
        {
        };

        template <typename T>
        struct IsCStyleFunction : public std::false_type
        {
        };

        template <typename Ret, typename... Args>
        struct IsCStyleFunction<Ret (&)(Args...)> : public std::true_type
        {
        };

        //
        template <typename Cond, typename T>
        using Requires = typename std::enable_if<Cond::value, T>::type;

    public:
        using result_type = RetType;
        // 默认构造函数创建一个空的function call wrapper
        move_only_function() noexcept : _function_base() {}

        move_only_function(std::nullptr_t) noexcept : _function_base() {}

        // delete 拷贝构造函数
        move_only_function(const move_only_function &) = delete;

        move_only_function(move_only_function &&other) noexcept : _function_base()
        {
            other.swap(*this);
        }

        template <typename Functor,
                  typename = Requires<std::negation<std::is_same<std::remove_reference_t<Functor>, move_only_function>>, void>,
                  typename = Requires<std::negation<IsCStyleFunction<Functor>>, void>,
                  typename = Requires<NotMoveOnlyCallable<Functor>, void>>
        move_only_function(Functor &&f) : _function_base()
        {
            using MyHandler = detail::FunctionHandler<RetType(ArgTypes...), std::remove_reference_t<Functor>>;
            if (MyHandler::_m_not_empty_function(f))
            {
                MyHandler::m_init_function(_m_functor, std::forward<Functor>(f));
                _m_invoker = &MyHandler::m_invoke;
                _m_manager = &MyHandler::_m_manager;
            }
        }

        // fix error: invalid application of 'sizeof' to a function type
        // [-Werror=pointer-arith]
        template <typename Res, typename... Args>
        move_only_function(Res (&f)(Args...)) : _function_base()
        {
            using MyHandler = detail::FunctionHandler<RetType(ArgTypes...), Res (*)(Args...)>;
            if (MyHandler::_m_not_empty_function(&f))
            {
                MyHandler::_m_init_functor(_m_functor, &f);
                _m_invoker = &MyHandler::_m_invoke;
                _m_manager = &MyHandler::_m_manager;
            }
        }

        move_only_function &operator=(const move_only_function &) = delete;
        move_only_function &operator=(move_only_function &&other)
        {
            move_only_function(std::move(other)).swap(*this);
            return *this;
        }

        move_only_function &operator=(std::nullptr_t) noexcept
        {
            if (_m_manager)
            {
                _m_manager(_m_functor, _m_functor, detail::_manager_operation::_destroy_functor);
                _m_manager = nullptr;
                _m_invoker = nullptr;
            }
            return *this;
        }

        template <typename Functor>
        Requires<NotMoveOnlyCallable<typename std::decay<Functor>::type>, move_only_function &>
        operator=(Functor &&f)
        {
            move_only_function(std::forward<Functor>(f)).swap(*this);
            return *this;
        }

        void swap(move_only_function &other) noexcept
        {
            std::swap(_m_functor, other._m_functor);
            std::swap(_m_manager, other._m_manager);
            std::swap(_m_invoker, other._m_invoker);
        }

        // _m_manager
        explicit operator bool() const noexcept
        {
            return !_m_empty();
        }

        RetType operator()(ArgTypes... _args) const
        {
            if (_m_empty())
            {
                throw std::bad_function_call();
            }
            return _m_invoker(_m_functor, std::forward<ArgTypes>(_args)...);
        }

    private:
        using InvokerType = RetType (*)(const detail::_any_data &, ArgTypes &&...);
        InvokerType _m_invoker;
    };

    namespace detail
    {
        template <typename>
        struct _move_only_function_guide_helper
        {
        };

        template <typename Res, typename T, typename... Args>
        struct _move_only_function_guide_helper<Res (T::*)(Args...)>
        {
            using type = Res(Args...);
        };

        template <typename Res, typename T, typename... Args>
        struct _move_only_function_guide_helper<Res (T::*)(Args...) &>
        {
            using type = Res(Args...);
        };

        template <typename Res, typename T, typename... Args>
        struct _move_only_function_guide_helper<Res (T::*)(Args...) const>
        {
            using type = Res(Args...);
        };

        template <typename Res, typename T, typename... Args>
        struct _move_only_function_guide_helper<Res (T::*)(Args...) const &>
        {
            using type = Res(Args...);
        };
    } // namespace detail

    template <typename Functor, typename Signature = typename detail::_move_only_function_guide_helper<decltype(&Functor::operator())>::type>
    move_only_function(Functor) -> move_only_function<Signature>;

    template <typename Res, typename... Args>
    inline void swap(move_only_function<Res(Args...)> &_x, move_only_function<Res(Args...)> &_y) noexcept
    {
        _x.swap(_y);
    }

    template <typename Res, typename... Args>
    inline bool operator==(const move_only_function<Res(Args...)> &f, std::nullptr_t) noexcept
    {
        return !static_assert<bool>(f);
    }

} // namespace async_framework::util

#endif