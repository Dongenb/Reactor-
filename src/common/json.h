#pragma once

#include <map>
#include <string>

namespace chat {

std::string json_escape(const std::string& value);
std::string json_object(const std::map<std::string, std::string>& fields);
std::string json_error(int code, const std::string& message);
bool json_get_string(const std::string& json, const std::string& key, std::string& value);

}  // namespace chat
