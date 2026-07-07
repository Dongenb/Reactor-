#include "server/chat_server.h"

#include "common/json.h"
#include "common/protocol.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

namespace chat {
namespace {

constexpr int kBacklog = 1024;
constexpr int kMaxEvents = 256;
constexpr auto kHeartbeatTimeout = std::chrono::seconds(35);
constexpr auto kSweepInterval = std::chrono::seconds(1);

}  // namespace

ChatServer::ChatServer(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port), last_sweep_(std::chrono::steady_clock::now()) {}

bool ChatServer::run() {
    ignore_sigpipe();
    if (!setup()) {
        return false;
    }

    std::cout << "chat_server listening on " << host_ << ":" << port_ << std::endl;
    std::vector<epoll_event> events(kMaxEvents);
    while (true) {
        // 单线程 Reactor 主循环：等待就绪事件，再分发到 accept/read/write 处理器。
        int n = ::epoll_wait(epoll_fd_.get(), events.data(), static_cast<int>(events.size()), 1000);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            if (fd == listen_fd_.get()) {
                accept_clients();
                continue;
            }
            if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                close_connection(fd, "socket closed");
                continue;
            }
            if (ev & EPOLLIN) {
                handle_read(fd);
            }
            if (connections_.find(fd) != connections_.end() && (ev & EPOLLOUT)) {
                handle_write(fd);
            }
        }
        sweep_idle_connections();
    }
}

bool ChatServer::setup() {
    std::string error;
    listen_fd_ = create_server_socket(host_, port_, kBacklog, error);
    if (!listen_fd_) {
        std::cerr << "create_server_socket failed: " << error << std::endl;
        return false;
    }
    epoll_fd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!epoll_fd_) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_.get();
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, listen_fd_.get(), &event) != 0) {
        std::cerr << "epoll_ctl listen failed: " << std::strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void ChatServer::accept_clients() {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int client_fd = ::accept(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << std::endl;
            return;
        }
        if (!set_nonblocking(client_fd)) {
            std::cerr << "set_nonblocking failed: " << std::strerror(errno) << std::endl;
            ::close(client_fd);
            continue;
        }

        // 新连接只关注可读事件；当输出缓冲区有数据时再动态打开 EPOLLOUT。
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = client_fd;
        if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, client_fd, &event) != 0) {
            std::cerr << "epoll_ctl client failed: " << std::strerror(errno) << std::endl;
            ::close(client_fd);
            continue;
        }

        Connection conn;
        conn.fd.reset(client_fd);
        conn.last_seen = std::chrono::steady_clock::now();
        connections_.emplace(client_fd, std::move(conn));
        std::cout << "client connected fd=" << client_fd << std::endl;
    }
}

void ChatServer::handle_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    auto& conn = it->second;
    uint8_t buf[4096];
    while (true) {
        // 一次事件中尽量读空内核缓冲区，读到 EAGAIN 表示本轮可读数据处理完毕。
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn.input.insert(conn.input.end(), buf, buf + n);
            conn.last_seen = std::chrono::steady_clock::now();
            continue;
        }
        if (n == 0) {
            close_connection(fd, "peer closed");
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close_connection(fd, std::strerror(errno));
        return;
    }

    std::vector<Message> messages;
    std::string error;
    if (!decode_stream(conn.input, messages, error)) {
        // 协议错误通常说明客户端异常或恶意输入，返回错误后主动断开连接。
        enqueue(fd, MessageType::Error, 0, json_error(400, error));
        handle_write(fd);
        close_connection(fd, "protocol error: " + error);
        return;
    }
    for (const auto& message : messages) {
        if (connections_.find(fd) == connections_.end()) {
            return;
        }
        handle_message(fd, message);
    }
}

void ChatServer::handle_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    auto& out = it->second.output;
    while (!out.empty()) {
        // 输出缓冲可能无法一次写完，剩余数据继续留在连接对象里等待下次 EPOLLOUT。
        ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            out.erase(out.begin(), out.begin() + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        close_connection(fd, std::strerror(errno));
        return;
    }
    if (connections_.find(fd) != connections_.end()) {
        flush_interest(fd);
    }
}

void ChatServer::handle_message(int fd, const Message& message) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    auto& conn = it->second;
    conn.last_seen = std::chrono::steady_clock::now();

    if (message.type == MessageType::Heartbeat) {
        // 心跳只刷新活跃时间并返回 ACK，不参与聊天业务广播。
        enqueue(fd, MessageType::Ack, message.request_id, json_object({{"event", "heartbeat"}}));
        return;
    }

    if (message.type == MessageType::Login) {
        std::string username;
        if (!json_get_string(message.payload, "username", username) || username.empty()) {
            enqueue(fd, MessageType::Error, message.request_id, json_error(400, "username required"));
            return;
        }
        if (users_.find(username) != users_.end()) {
            enqueue(fd, MessageType::Error, message.request_id, json_error(409, "username already exists"));
            return;
        }
        if (conn.logged_in) {
            users_.erase(conn.username);
        }
        conn.username = username;
        conn.logged_in = true;
        users_[username] = fd;
        enqueue(fd, MessageType::Ack, message.request_id, json_object({{"event", "login"}, {"username", username}}));
        // 上线/离线通知复用 BROADCAST 类型，方便客户端统一展示聊天室事件。
        broadcast(0, json_object({{"event", "online"}, {"username", username}}));
        return;
    }

    if (!conn.logged_in) {
        enqueue(fd, MessageType::Error, message.request_id, json_error(401, "login required"));
        return;
    }

    if (message.type == MessageType::Logout) {
        enqueue(fd, MessageType::Ack, message.request_id, json_object({{"event", "logout"}}));
        handle_write(fd);
        close_connection(fd, "logout");
        return;
    }

    if (message.type == MessageType::Broadcast) {
        std::string text;
        if (!json_get_string(message.payload, "message", text)) {
            enqueue(fd, MessageType::Error, message.request_id, json_error(400, "message required"));
            return;
        }
        broadcast(message.request_id, json_object({{"from", conn.username}, {"message", text}}));
        return;
    }

    if (message.type == MessageType::PrivateMessage) {
        std::string target;
        std::string text;
        if (!json_get_string(message.payload, "to", target) || !json_get_string(message.payload, "message", text)) {
            enqueue(fd, MessageType::Error, message.request_id, json_error(400, "to and message required"));
            return;
        }
        auto user_it = users_.find(target);
        if (user_it == users_.end()) {
            enqueue(fd, MessageType::Error, message.request_id, json_error(404, "target user not found"));
            return;
        }
        enqueue(user_it->second, MessageType::PrivateMessage, message.request_id,
                json_object({{"from", conn.username}, {"message", text}, {"to", target}}));
        if (user_it->second != fd) {
            enqueue(fd, MessageType::Ack, message.request_id, json_object({{"event", "private_sent"}, {"to", target}}));
        }
        return;
    }

    enqueue(fd, MessageType::Error, message.request_id, json_error(400, "unsupported client message type"));
}

void ChatServer::close_connection(int fd, const std::string& reason) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    std::string username = it->second.username;
    bool was_logged_in = it->second.logged_in;
    if (was_logged_in) {
        users_.erase(username);
    }
    // 先从 epoll 删除，再擦除连接；UniqueFd 析构会关闭 fd。
    ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    connections_.erase(it);
    std::cout << "client closed fd=" << fd << " reason=" << reason << std::endl;
    if (was_logged_in) {
        broadcast(0, json_object({{"event", "offline"}, {"username", username}}));
    }
}

void ChatServer::enqueue(int fd, MessageType type, uint32_t request_id, const std::string& payload) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    auto bytes = encode_message({type, request_id, payload});
    it->second.output.insert(it->second.output.end(), bytes.begin(), bytes.end());
    flush_interest(fd);
}

void ChatServer::flush_interest(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    if (!it->second.output.empty()) {
        // 只有待发送数据时才监听 EPOLLOUT，避免写事件持续触发造成忙等。
        event.events |= EPOLLOUT;
    }
    event.data.fd = fd;
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &event) != 0) {
        close_connection(fd, "epoll mod failed");
    }
}

void ChatServer::broadcast(uint32_t request_id, const std::string& payload) {
    std::vector<int> fds;
    fds.reserve(connections_.size());
    for (const auto& [fd, conn] : connections_) {
        if (conn.logged_in) {
            fds.push_back(fd);
        }
    }
    for (int fd : fds) {
        enqueue(fd, MessageType::Broadcast, request_id, payload);
    }
}

void ChatServer::sweep_idle_connections() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_sweep_ < kSweepInterval) {
        return;
    }
    last_sweep_ = now;

    std::vector<int> expired;
    for (const auto& [fd, conn] : connections_) {
        // 客户端每 30 秒心跳一次；服务端留 35 秒容忍网络抖动。
        if (now - conn.last_seen > kHeartbeatTimeout) {
            expired.push_back(fd);
        }
    }
    for (int fd : expired) {
        close_connection(fd, "heartbeat timeout");
    }
}

}  // namespace chat
