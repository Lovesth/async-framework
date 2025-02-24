//
// Created by xmh on 25-2-19.
//
#include <async_simple/coro/FutureAwaiter.h>
#include <async_simple/executors/SimpleExecutor.h>
#include <netinet/in.h>
#include <memory>
#include <thread>
#include "HookSysCall.hpp"

async_simple::coro::Lazy<> echo_server_impl(int fd, IoContext* io_context) {
    char buffer[2048] = {0};
    Socket sock(fd, io_context);
    while (true) {
        // receive
        auto recv_len = co_await recv(&sock, buffer, sizeof(buffer));
        std::cout << "recv: " << recv_len << " bytes" << std::endl;
        if (recv_len < 0) {
            std::cerr << "Error receive message!" << std::endl;
            co_return;
        }
        if (recv_len == 0) {
            std::cout << "socket is closed by client!" << std::endl;
            co_return;
        }
        // send
        int send_len = 0;
        while (send_len < recv_len) {
            auto res = co_await send(&sock, buffer + send_len, recv_len - send_len);
            std::cout << "send: " << res << " bytes" << std::endl;
            if (res <= 0) {
                std::cerr << "Error send message back!"
                          << std::endl;
                co_return;
            }
            send_len += res;
        }
    }
    std::cout << "echo_server_impl finished!" << std::endl;
    co_return;
}

async_simple::coro::Lazy<> echo_server(Socket* server_sock) {
    auto executor_ = co_await async_simple::CurrentExecutor{};
    auto io_context = server_sock->io_context_;
    async_simple::logicAssert(executor_,"executor is not allowed to be nullptr here!");
    while (true) {
        auto fd = co_await accept(server_sock);
        if (fd == -1) {
            ::close(server_sock->fd_);
            server_sock->fd_ = -1;
            co_return;
        }
        executor_->schedule([fd, io_context]() -> void {
            echo_server_impl(fd, io_context).start([](auto&&) {});
        });
    }
    co_return;
}

int main() {
    // 1. 创建 Socket
    int server_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == -1) {
        std::cerr << "Failed to create server socket!" << std::endl;
        return -1;
    }

    // 2. 设置地址复用
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. 设置地址和端口
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9980);

    // 4. 绑定地址和端口号到套接字
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr)) ==
        -1) {
        std::cerr << "Failed to bind!" << std::endl;
        return -1;
    }

    // 监听模式设置
    if (listen(server_fd, 2048) == -1) {
        std::cerr << "Listen failed!" << std::endl;
        return -1;
    }

    //
    async_simple::executors::SimpleExecutor executor{16};
    IoContext io_context(100, &executor);
    Socket server_sock(server_fd, &io_context);

    auto t = std::jthread(&IoContext::run, &io_context);
    echo_server(&server_sock).directlyStart([](auto&&) {}, &executor);
    return 0;
}