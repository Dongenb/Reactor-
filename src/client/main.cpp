#include "common/json.h"
#include "common/protocol.h"
#include "common/socket_utils.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

uint16_t parse_port(const std::string& value) {
    int port = std::atoi(value.c_str());
    if (port <= 0 || port > 65535) {
        return 9000;
    }
    return static_cast<uint16_t>(port);
}

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--host 127.0.0.1] [--port 9000]\n";
}

bool send_message(int fd, chat::MessageType type, uint32_t request_id, const std::string& payload) {
    auto bytes = chat::encode_message({type, request_id, payload});
    size_t sent = 0;
    while (sent < bytes.size()) {
        ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

void receive_loop(int fd, std::atomic<bool>& running) {
    std::vector<uint8_t> input;
    uint8_t buf[4096];
    while (running.load()) {
        // 接收线程持续解析服务端推送；半包会留在 input 中等待后续数据。
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            input.insert(input.end(), buf, buf + n);
            std::vector<chat::Message> messages;
            std::string error;
            if (!chat::decode_stream(input, messages, error)) {
                std::cerr << "protocol error: " << error << std::endl;
                running.store(false);
                break;
            }
            for (const auto& message : messages) {
                std::cout << "[" << chat::message_type_name(message.type) << "] "
                          << message.payload << std::endl;
            }
            continue;
        }
        if (n == 0) {
            std::cerr << "server closed connection" << std::endl;
            running.store(false);
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        std::cerr << "recv failed" << std::endl;
        running.store(false);
        break;
    }
}

void heartbeat_loop(int fd, std::atomic<bool>& running, std::atomic<bool>& logged_in, std::atomic<uint32_t>& request_id) {
    auto next = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        if (running.load() && logged_in.load() && now >= next) {
            // 登录后每 30 秒发送一次心跳，配合服务端的 35 秒超时回收。
            send_message(fd, chat::MessageType::Heartbeat, request_id.fetch_add(1), "{}");
            next = now + std::chrono::seconds(30);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 9000;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = parse_port(argv[++i]);
        } else if (arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    chat::ignore_sigpipe();
    std::string error;
    chat::UniqueFd fd = chat::connect_to_server(host, port, error);
    if (!fd) {
        std::cerr << "connect failed: " << error << std::endl;
        return 1;
    }

    std::atomic<bool> running{true};
    std::atomic<bool> logged_in{false};
    std::atomic<uint32_t> request_id{1};
    std::thread receiver(receive_loop, fd.get(), std::ref(running));
    std::thread heartbeat(heartbeat_loop, fd.get(), std::ref(running), std::ref(logged_in), std::ref(request_id));

    std::cout << "Commands: /login name, /all text, /to user text, /quit" << std::endl;
    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
        if (line.rfind("/login ", 0) == 0) {
            std::string username = line.substr(7);
            logged_in.store(true);
            send_message(fd.get(), chat::MessageType::Login, request_id.fetch_add(1),
                         chat::json_object({{"username", username}}));
        } else if (line.rfind("/all ", 0) == 0) {
            send_message(fd.get(), chat::MessageType::Broadcast, request_id.fetch_add(1),
                         chat::json_object({{"message", line.substr(5)}}));
        } else if (line.rfind("/to ", 0) == 0) {
            size_t split = line.find(' ', 4);
            if (split == std::string::npos) {
                std::cerr << "usage: /to user text" << std::endl;
                continue;
            }
            std::string to = line.substr(4, split - 4);
            std::string text = line.substr(split + 1);
            send_message(fd.get(), chat::MessageType::PrivateMessage, request_id.fetch_add(1),
                         chat::json_object({{"message", text}, {"to", to}}));
        } else if (line == "/quit") {
            send_message(fd.get(), chat::MessageType::Logout, request_id.fetch_add(1), "{}");
            running.store(false);
            break;
        } else if (!line.empty()) {
            std::cerr << "unknown command" << std::endl;
        }
    }

    running.store(false);
    ::shutdown(fd.get(), SHUT_RDWR);
    if (receiver.joinable()) {
        receiver.join();
    }
    if (heartbeat.joinable()) {
        heartbeat.join();
    }
    return 0;
}
