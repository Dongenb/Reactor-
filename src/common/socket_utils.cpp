#include "common/socket_utils.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace chat {

UniqueFd::~UniqueFd() {
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
}

void UniqueFd::reset(int fd) {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

void ignore_sigpipe() {
    // 对端关闭后继续 send 可能触发 SIGPIPE；忽略它，让 send 返回错误由业务层处理。
    ::signal(SIGPIPE, SIG_IGN);
}

bool set_nonblocking(int fd) {
    // Reactor 模型要求 fd 非阻塞，否则单个慢连接会卡住整个事件循环。
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

UniqueFd create_server_socket(const std::string& host, uint16_t port, int backlog, std::string& error) {
    UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        error = std::strerror(errno);
        return {};
    }

    int yes = 1;
    ::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (!set_nonblocking(fd.get())) {
        error = std::strerror(errno);
        return {};
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        error = "invalid host: " + host;
        return {};
    }
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error = std::strerror(errno);
        return {};
    }
    if (::listen(fd.get(), backlog) != 0) {
        error = std::strerror(errno);
        return {};
    }
    return fd;
}

UniqueFd connect_to_server(const std::string& host, uint16_t port, std::string& error) {
    UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        error = std::strerror(errno);
        return {};
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        error = "invalid host: " + host;
        return {};
    }
    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error = std::strerror(errno);
        return {};
    }
    return fd;
}

}  // namespace chat
