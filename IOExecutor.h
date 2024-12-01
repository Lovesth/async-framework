#ifndef ASYNC_FRAMEWORK_IOEXECUTOR_H
#define ASYNC_FRAMEWORK_IOEXECUTOR_H

#include <cstdint>
#include <functional>

namespace async_framework
{
    // IOExecutor accepts and performs io requests, callers will be notified by
    // callback. IO type and arguments are similar to Linux AIO.
    enum iocb_cmd
    {
        IOCB_CMD_PREAD = 0,
        IOCB_CMD_PWRITE = 1,
        IOCB_CMD_FSYNC = 2,
        IOCB_CMD_FDSYNC = 3,
        /* These two are experimental.
         * IOCB_CMD_PREADX = 4,
         * IOCB_CMD_POLL = 5,
         */
        IOCB_CMD_NOOP = 6,
        IOCB_CMD_PREADV = 7,
        IOCB_CMD_PWRITEV = 8,
    };

    struct io_event_t
    {
        void *data;
        void *obj;
        uint64_t res;
        uint64_t res2;
    };

    struct iovec_t
    {
        void *iov_base;
        size_t iov_len;
    };

    using AIOCallback = std::function<void(io_event_t &)>;
    // The IOExecutor would accept IO read/write requests.
    // After the user implements an IOExecutor, he should associate
    // the IOExecutor with the corresponding Executor implementation.

    class IOExecutor
    {
    public:
        using Func = std::function<void()>;
        IOExecutor() {}
        virtual ~IOExecutor() {}
        IOExecutor(const IOExecutor &) = delete;
        IOExecutor &operator=(const IOExecutor &) = delete;

    public:
        virtual void submitIO(int fd, iocb_cmd cmd, void *buffer, size_t length, __off_t offset, AIOCallback cbfn) = 0;
        virtual void submitIOV(int fd, iocb_cmd cmd, const iovec_t *iov, size_t count, __off_t offset, AIOCallback cbfn) = 0;
    };
} // namespace async_framework

#endif