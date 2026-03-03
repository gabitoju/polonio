#include "polonio/server/http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace polonio {
namespace {

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

void send_fixed_response(int client_fd) {
    static constexpr char kResponse[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "OK";
    const std::size_t total = sizeof(kResponse) - 1;
    std::size_t written = 0;
    while (written < total) {
        ssize_t sent = ::send(client_fd, kResponse + written, total - written, 0);
        if (sent <= 0) {
            break;
        }
        written += static_cast<std::size_t>(sent);
    }
}

} // namespace

void run_http_server(const ServerConfig& config) {
    std::error_code ec;
    auto absolute_root = std::filesystem::absolute(config.root, ec);
    if (ec) {
        absolute_root = config.root;
    }
    std::cerr << "Serving " << absolute_root << " on http://localhost:" << config.port << std::endl;

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw_system_error("socket");
    }

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        ::close(server_fd);
        errno = err;
        throw_system_error("bind");
    }

    if (::listen(server_fd, 1) < 0) {
        int err = errno;
        ::close(server_fd);
        errno = err;
        throw_system_error("listen");
    }

    sockaddr_in client{};
    socklen_t client_len = sizeof(client);
    int client_fd = -1;
    while (client_fd < 0) {
        client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client), &client_len);
        if (client_fd < 0 && errno == EINTR) {
            continue;
        }
        if (client_fd < 0) {
            int err = errno;
            ::close(server_fd);
            errno = err;
            throw_system_error("accept");
        }
    }

    send_fixed_response(client_fd);
    ::close(client_fd);
    ::close(server_fd);
}

} // namespace polonio
