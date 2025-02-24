//
// Created by xmh on 25-2-21.
//
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <asio/detail/socket_ops.hpp>
#include "HookSysCall.hpp"

async_simple::coro::Lazy<> client_send_impl(const sockaddr_in server_addr, IoContext *io_context, int nRound) {
    Socket sock(AF_INET, SOCK_STREAM, 0, io_context);
    auto res = co_await connect(&sock, reinterpret_cast<const sockaddr *>((&server_addr)));
    if (res == -1) {
        co_return;
    }
    char buffer[] = "Hello, this is coro_epoll_client";
    int bufferSize = sizeof(buffer);
    for (int j = 0; j < nRound; ++j) {
        int send_bytes{0};
        while (send_bytes < bufferSize) {
            auto tmp = co_await send(&sock, buffer + send_bytes,bufferSize - send_bytes);
            std::cout << "Send byte cnt: " << tmp << std::endl;
            if (tmp <= 0) {
                std::cerr << "Error send message!" << std::endl;
                co_return;
            }
            send_bytes += tmp;
        }
        int recv_bytes{0};
        while (recv_bytes < bufferSize) {
            auto tmp = co_await recv(&sock, buffer + recv_bytes,
                                     bufferSize - recv_bytes);
            std::cout << "recv count: " << tmp << std::endl;
            if (tmp < 0) {
                std::cerr << "Error recv message!" << std::endl;
                co_return;
            }
            if (tmp == 0) {
                std::cerr << "socket is closed by server!" << std::endl;
                co_return;
            }
            recv_bytes += tmp;
        }
    }
    std::cout << "Send message nRound" << std::endl;
    co_return;
}

async_simple::coro::Lazy<> client_send(IoContext *io_context, std::string host,
                                       int port, int nClients = 1024,
                                       int nRound = 1024) {
    auto executor_ = co_await async_simple::CurrentExecutor{};
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) != 1) {
        std::cerr << "Error convert string to addr!" << std::endl;
        co_return;
    }
    server_addr.sin_port = htons(port);

    // 创建nClients个客户端
    for (int i = 0; i < nClients; ++i) {
        executor_->schedule(
            [server_addr, io_context, nRound]() -> void {
                client_send_impl(server_addr, io_context, nRound).start([](auto &&) {});
        });
    }
}

int main() {
    async_simple::executors::SimpleExecutor executor{16};
    IoContext io_context(100, &executor);

    auto t = std::jthread(&IoContext::run, &io_context);
    client_send(&io_context, "127.0.0.1", 9980, 1000, 1000)
        .directlyStart([](auto &&) {}, &executor);
    return 0;
}