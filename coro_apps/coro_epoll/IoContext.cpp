//
// Created by xmh on 25-2-20.
//
#include "IoContext.h"
#include "Socket.h"
#include "async_simple/coro/SpinLock.h"

IoContext::IoContext(int maxEvents,
                     async_simple::Executor *executor)
    : maxEvents_(maxEvents), executor_(executor) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        std::cerr << "Error creating epoll!" << std::endl;
        exit(-1);
    }
    if (maxEvents_ <= 0) {
        std::cerr << "Could not schedule coroutine!" << std::endl;
        exit(-1);
    }
    eventPool_ = new epoll_event[maxEvents_];
}

IoContext::IoContext(IoContext &&other) {
    epoll_fd_ = std::exchange(other.epoll_fd_, -1);
    maxEvents_ = std::exchange(other.maxEvents_, 0);
    executor_ = std::exchange(other.executor_, nullptr);
    eventPool_ = std::exchange(other.eventPool_, nullptr);
}

IoContext &IoContext::operator=(IoContext &&other) {
    std::swap(epoll_fd_, other.epoll_fd_);
    std::swap(maxEvents_, other.maxEvents_);
    std::swap(executor_, other.executor_);
    std::swap(eventPool_, other.eventPool_);
    return *this;
}

IoContext::~IoContext() {
    delete[] eventPool_;
}

void IoContext::run() {
    while (true) {
        int nfds = epoll_wait(epoll_fd_, eventPool_, maxEvents_, -1);
        for (int i = 0; i < nfds; ++i) {
            auto sock = static_cast<Socket *>(eventPool_[i].data.ptr);
            const auto events = eventPool_[i].events;
            async_simple::coro::ScopedSpinLock Lock(sock->coro_lock_);
            // 隐式监听
            if (events & (EPOLLERR | EPOLLHUP)) {
                // 出错或者关闭，交给上层处理错误
                sock->recv_event_ = events;
                sock->send_event_ = events;
                if (auto h = std::exchange(sock->coro_recv_, nullptr); h) {
                    executor_->schedule([h] { h.resume(); });
                }
                if (auto h = std::exchange(sock->coro_send_, nullptr); h) {
                    executor_->schedule([h] { h.resume(); });
                }
                continue;
            }

            if (events & EPOLLRDHUP) {
                auto h = std::exchange(sock->coro_recv_, nullptr);
                sock->recv_event_ = events;
                if (h) {
                    executor_->schedule([h] { h.resume(); });
                }
            }

            if (events & EPOLLIN) {
                auto h = std::exchange(sock->coro_recv_, nullptr);
                sock->recv_event_ = events;
                if (h) {
                    executor_->schedule([h] { h.resume(); });
                }
            }

            if (events & EPOLLOUT) {
                auto h = std::exchange(sock->coro_send_, nullptr);
                sock->send_event_ = events;
                if (h) {
                    executor_->schedule([h] { h.resume(); });
                }
            }
        }
    }
}
