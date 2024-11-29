#ifndef ASYNC_FRAMEWORK_COMMON_MACROS_H
#define ASYNC_FRAMEWORK_COMMON_MACROS_H

#if __has_cpp_attribute(likely) && __has_cpp_attribute(unlikely)
#define AS_LIKELY [[likely]]
#define AS_UNLIKELY [[unlikely]]
#else
#define AS_LIKELY
#define AS_UNLIKELY
#endif

#ifdef _WIN32
#define AS_INLINE
#else
#define AS_INLINE __attribute__((__always_inline__)) inline
#endif

#ifdef __clang__
#if __has_feature(address_sanitizer)
#define AS_INTERNAL_USE_ASAN 1
#endif // __has_feature(address_sanitizer)
#endif // __clang__

#ifdef __GNUC__
#ifdef __SANITIZE_ADDRESS__ // GCC
#define AS_INTERNAL_USE_ASAN 1
#endif // __SANITIZE_ADDRESS__
#endif // __GNUC__

#if defined(__alibaba_clang__) && \
    __has_cpp_attribute(ACC::coro_only_destroy_when_complete)
#define CORO_ONLY_DESTROY_WHEN_DONE [[ACC::coro_only_destroy_when_complete]]
#else
#define CORO_ONLY_DESTROY_WHEN_DONE
#endif

#if defined(__alibaba_clang__) && \
    __has_cpp_attribute(ACC::elideable_after_await)
#define ELIDEABLE_AFTER_AWAIT [[ACC::elideable_after_await]]
#else
#define ELIDEABLE_AFTER_AWAIT
#endif

#endif
