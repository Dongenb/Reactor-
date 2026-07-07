#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat {

constexpr size_t kHeaderSize = 10;
constexpr uint32_t kMaxPayloadSize = 1024 * 1024;

enum class MessageType : uint16_t {
    Login = 1,
    Logout = 2,
    Broadcast = 3,
    PrivateMessage = 4,
    Heartbeat = 5,
    Ack = 6,
    Error = 7,
};

struct Message {
    MessageType type;
    uint32_t request_id;
    std::string payload;
};

std::vector<uint8_t> encode_message(const Message& message);
bool decode_stream(std::vector<uint8_t>& buffer, std::vector<Message>& messages, std::string& error);
std::string message_type_name(MessageType type);

}  // namespace chat
