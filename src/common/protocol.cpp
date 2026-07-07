#include "common/protocol.h"

#include <arpa/inet.h>
#include <cstring>
#include <utility>

namespace chat {

std::vector<uint8_t> encode_message(const Message& message) {
    std::vector<uint8_t> bytes(kHeaderSize + message.payload.size());
    uint32_t length = htonl(static_cast<uint32_t>(message.payload.size()));
    uint16_t type = htons(static_cast<uint16_t>(message.type));
    uint32_t request_id = htonl(message.request_id);

    std::memcpy(bytes.data(), &length, sizeof(length));
    std::memcpy(bytes.data() + 4, &type, sizeof(type));
    std::memcpy(bytes.data() + 6, &request_id, sizeof(request_id));
    std::memcpy(bytes.data() + kHeaderSize, message.payload.data(), message.payload.size());
    return bytes;
}

bool decode_stream(std::vector<uint8_t>& buffer, std::vector<Message>& messages, std::string& error) {
    // 按“能解析多少解析多少”的方式处理 TCP 流，天然支持半包和粘包。
    while (buffer.size() >= kHeaderSize) {
        uint32_t net_length = 0;
        uint16_t net_type = 0;
        uint32_t net_request_id = 0;
        std::memcpy(&net_length, buffer.data(), sizeof(net_length));
        std::memcpy(&net_type, buffer.data() + 4, sizeof(net_type));
        std::memcpy(&net_request_id, buffer.data() + 6, sizeof(net_request_id));

        uint32_t length = ntohl(net_length);
        uint16_t type = ntohs(net_type);
        uint32_t request_id = ntohl(net_request_id);
        if (length > kMaxPayloadSize) {
            error = "payload too large";
            return false;
        }
        if (type < static_cast<uint16_t>(MessageType::Login) ||
            type > static_cast<uint16_t>(MessageType::Error)) {
            error = "unknown message type";
            return false;
        }
        if (buffer.size() < kHeaderSize + length) {
            // 数据还不够一个完整包，保留缓冲区内容等待下一次 recv。
            break;
        }

        Message message;
        message.type = static_cast<MessageType>(type);
        message.request_id = request_id;
        message.payload.assign(
            reinterpret_cast<const char*>(buffer.data() + kHeaderSize),
            reinterpret_cast<const char*>(buffer.data() + kHeaderSize + length));
        messages.push_back(std::move(message));
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(kHeaderSize + length));
    }
    return true;
}

std::string message_type_name(MessageType type) {
    switch (type) {
        case MessageType::Login:
            return "LOGIN";
        case MessageType::Logout:
            return "LOGOUT";
        case MessageType::Broadcast:
            return "BROADCAST";
        case MessageType::PrivateMessage:
            return "PRIVATE_MSG";
        case MessageType::Heartbeat:
            return "HEARTBEAT";
        case MessageType::Ack:
            return "ACK";
        case MessageType::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace chat
