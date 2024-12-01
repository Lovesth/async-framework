#pragma once

#include "../IOExecutor.h"
#include <thread>
#include <libaio.h>

namespace async_framework
{
    namespace executors
    {
        // This is a demo IOExecutor.
        // submitIO and submitIOV should be implemented
        class SimpleIOExecutor : public IOExecutor
        {
        public:
            static constexpr int KMaxAio = 8;

        public:
            SimpleIOExecutor() {}
            virtual ~SimpleIOExecutor() {}
            SimpleIOExecutor(const IOExecutor &) = delete;
            SimpleIOExecutor &operator=(const IOExecutor &) = delete;

        public:
            class Task
            {
            public:
                Task(AIOCallback &func) : func_(func) {}
                ~Task() {}

            public:
                void process(io_event_t &event) { func_(event); }

            private:
                AIOCallback func_;
            };

        public:
            bool init()
            {
                auto r = io_setup(KMaxAio, &ioContext_);
                if (r < 0)
                {
                    return false;
                }
                loopThread_ = std::thread([this]() mutable
                                          { this->loop(); });
                return true;
            }

            void destroy()
            {
                shutdown_ = true;
                if (loopThread_.joinable())
                {
                    loopThread_.join();
                }
                io_destroy(ioContext_);
            }

            void loop()
            {
                while (!shutdown_)
                {
                    io_event events[KMaxAio];
                    struct timespec timeout = {0, 1000 * 300};
                    auto n = io_getevents(ioContext_, 1, KMaxAio, events, &timeout);
                    if (n < 0)
                    {
                        continue;
                    }
                    for (auto i = 0; i < n; i++)
                    {
                        auto task = reinterpret_cast<Task *>(events[i].data);
                        io_event_t evt{events[i].data, events[i].obj, events[i].res, events[i].res2};
                        task->process(evt);
                        delete task;
                    }
                }
            }

        public:
            void submitIO([[maybe_unused]] int fd, [[maybe_unused]] iocb_cmd cmd, [[maybe_unused]] void *buffer, [[maybe_unused]] size_t length,
                          [[maybe_unused]] off_t offset, [[maybe_unused]] AIOCallback cbfn) override
            {
                iocb io;
                memset(&io, 0, sizeof(iocb));
                io.aio_fildes = fd;
                io.aio_lio_opcode = cmd;
                io.u.c.buf = buffer;
                io.u.c.offset = offset;
                io.u.c.nbytes = length;
                io.data = new Task(cbfn);

                struct iocb *iocbs[] = {&io};
                auto r = io_submit(ioContext_, 1, iocbs);
                if (r < 0)
                {
                    auto task = reinterpret_cast<Task *>(iocbs[0]->data);
                    io_event_t event;
                    event.res = r;
                    task->process(event);
                    delete task;
                    return;
                }
            }

            void submitIOV([[maybe_unused]] int fd, [[maybe_unused]] iocb_cmd cmd, [[maybe_unused]] const iovec_t *iov,
                           [[maybe_unused]] size_t count, [[maybe_unused]] off_t offset, [[maybe_unused]] AIOCallback cbfn) override
            {
                iocb io;
                memset(&io, 0, sizeof(iocb));
                io.aio_fildes = fd;
                io.aio_lio_opcode = cmd;
                io.u.c.buf = (void *)iov;
                io.u.c.offset = offset;
                io.u.c.nbytes = count;
                io.data = new Task(cbfn);

                struct iocb *iocbs[] = {&io};

                auto r = io_submit(ioContext_, 1, iocbs);
                if (r < 0)
                {
                    auto task = reinterpret_cast<Task *>(iocbs[0]->data);
                    io_event_t event;
                    event.res = r;
                    task->process(event);
                    delete task;
                    return;
                }
            }

        private:
            volatile bool shutdown_ = false;
            io_context_t ioContext_ = 0;
            std::thread loopThread_;
        };
    } // namespace executors
} // namespace async_framework