#include "server/chat_server.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

uint16_t parse_port(const std::string& value) {
    int port = std::atoi(value.c_str());
    if (port <= 0 || port > 65535) {
        return 9000;
    }
    return static_cast<uint16_t>(port);
}

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [--host 0.0.0.0] [--port 9000]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "0.0.0.0";
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

    chat::ChatServer server(host, port);
    return server.run() ? 0 : 1;
}
