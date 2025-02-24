//
// Created by xmh on 25-2-13.
//

#ifndef SOCKET_H
#define SOCKET_H

#include <IoContext.h>
#include <async_simple/coro/SpinLock.h>
#include <async_simple/executors/SimpleExecutor.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <coroutine>
#include <cstdint>
#include <iostream>
#include <memory>

// 一个Socket最多可被两个线程操作（一个读一个写）
class IoContext;
class Socket {
public:
    // enum class state : uint32_t {
    //     IN = 1, // 可读
    //     OUT = 1<<1, // 可写
    //     ERR = 1<<2, // 发生错误
    //     HUP = 1<<3, // 双方都断开连接
    //     RDHUP = 1<<4, // （对方发送FIN后不再继续发送数据）
    //     ET = 1<<5, // ET模式
    //     ONESHOT = 1<<6, // 只监听一次事件，事件触发之后从epoll移除
    // };
    Socket(int domain, int type, int protocol, IoContext *io_context, uint32_t listen_events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET));
    Socket(int fd, IoContext *io_context, uint32_t listen_events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET));
    ~Socket();

    bool attach2IoContext();
    bool addEvents(const uint32_t events);
    bool removeEvents(uint32_t events);

public:
    friend class IoContext;

public:
    int fd_ = -1;
    IoContext *io_context_ = nullptr;
    // 当前监听的事件，epoll需要用modify所以需要将旧的事件保存起来
    uint32_t listen_events_;
    // 实际监听到的事
    uint32_t waited_events_{0};
    // spin lock for listen_events_
    async_simple::coro::SpinLock io_state_lock_{};
    // 等待在当前socket的协程
    std::coroutine_handle<> h_{nullptr};
};

struct SocketAwaiter {
    auto coAwait(auto&&) {
        return *this;
    }
    static bool await_ready() noexcept {
        return false;
    }
    bool await_suspend(const std::coroutine_handle<> h) const noexcept{
        if (!sock->io_context_ || sock->io_context_->epoll_fd_==-1)
            return false;
        sock->h_ = h;
        return sock->attach2IoContext();
    }
    auto await_resume() const noexcept {
        return sock->waited_events_;
    }

    SocketAwaiter(Socket* s) {
        sock = s;
    }
    Socket* sock;
};

#endif  // SOCKET_H
