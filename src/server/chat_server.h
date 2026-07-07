#pragma once

#include "common/protocol.h"
#include "common/socket_utils.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat {

class ChatServer {
public:
    ChatServer(std::string host, uint16_t port);
    bool run();

private:
    struct Connection {
        UniqueFd fd;
        std::string username;
        bool logged_in = false;
        std::vector<uint8_t> input;
        std::vector<uint8_t> output;
        std::chrono::steady_clock::time_point last_seen;
    };

    bool setup();
    void accept_clients();
    void handle_read(int fd);
    void handle_write(int fd);
    void handle_message(int fd, const Message& message);
    void close_connection(int fd, const std::string& reason);
    void enqueue(int fd, MessageType type, uint32_t request_id, const std::string& payload);
    void flush_interest(int fd);
    void broadcast(uint32_t request_id, const std::string& payload);
    void sweep_idle_connections();

    std::string host_;
    uint16_t port_;
    UniqueFd listen_fd_;
    UniqueFd epoll_fd_;
    std::unordered_map<int, Connection> connections_;
    std::unordered_map<std::string, int> users_;
    std::chrono::steady_clock::time_point last_sweep_;
};

}  // namespace chat
