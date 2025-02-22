//
// Created by xmh on 25-2-13.
//

#ifndef SOCKET_H
#define SOCKET_H

#include <async_simple/coro/SpinLock.h>
#include <sys/epoll.h>
#include <coroutine>
#include <cstdint>
#include <fcntl.h>
#include <iostream>

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
    Socket(int domain, int type, int protocol, IoContext *io_context, uint32_t listen_events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP));
    Socket(int fd, IoContext *io_context, uint32_t listen_events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP));
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) = delete;
    Socket &operator=(Socket &&other) = delete;
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
    // 读协程实际监听到的事件
    uint32_t recv_event_{0};
    // 写协程实际监听到的事件
    uint32_t send_event_{0};
    // spin lock for coroutine
    async_simple::coro::SpinLock coro_lock_{};
    // spin lock for listen_events_
    async_simple::coro::SpinLock io_state_lock_{};
    // 因为可能有两个协程同时在等待一个socket，所以要用两个coroutine_handle来保存。
    std::coroutine_handle<> coro_recv_{nullptr};  // 接收数据的协程
    std::coroutine_handle<> coro_send_{nullptr};  // 发送数据的协程
};

struct SendAwaiter {
    Socket *sock;
    explicit SendAwaiter(Socket *s) : sock(s) {}
    bool await_ready() noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        async_simple::coro::ScopedSpinLock Lock(sock->coro_lock_);
        if (sock->send_event_)
            return false;
        sock->coro_send_ = h;
        return true;
    }

    auto await_resume() noexcept { return std::exchange(sock->send_event_, 0); }
};

struct RecvAwaiter {
    Socket *sock;
    explicit RecvAwaiter(Socket *s) : sock(s) {}
    bool await_ready() noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        async_simple::coro::ScopedSpinLock Lock(sock->coro_lock_);
        if (sock->recv_event_)
            return false;
        sock->coro_recv_ = h;
        return true;
    }
    auto await_resume() noexcept { return std::exchange(sock->recv_event_, 0); }
};

#endif  // SOCKET_H
