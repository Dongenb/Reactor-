#pragma once

#include <cstdint>
#include <string>

namespace chat {

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd();

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;

    int get() const { return fd_; }
    int release();
    void reset(int fd = -1);
    explicit operator bool() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

void ignore_sigpipe();
bool set_nonblocking(int fd);
UniqueFd create_server_socket(const std::string& host, uint16_t port, int backlog, std::string& error);
UniqueFd connect_to_server(const std::string& host, uint16_t port, std::string& error);

}  // namespace chat
