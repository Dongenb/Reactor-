#include "common/json.h"

#include <cctype>
#include <sstream>

namespace chat {
namespace {

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

bool parse_string(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') {
        return false;
    }
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (i >= s.size()) {
                return false;
            }
            char e = s[i++];
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    return false;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

}  // namespace

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string json_object(const std::map<std::string, std::string>& fields) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
    }
    oss << "}";
    return oss.str();
}

std::string json_error(int code, const std::string& message) {
    return "{\"code\":" + std::to_string(code) + ",\"message\":\"" + json_escape(message) + "\"}";
}

bool json_get_string(const std::string& json, const std::string& key, std::string& value) {
    size_t i = 0;
    skip_ws(json, i);
    if (i >= json.size() || json[i++] != '{') {
        return false;
    }
    while (i < json.size()) {
        skip_ws(json, i);
        if (i < json.size() && json[i] == '}') {
            return false;
        }

        std::string parsed_key;
        if (!parse_string(json, i, parsed_key)) {
            return false;
        }
        skip_ws(json, i);
        if (i >= json.size() || json[i++] != ':') {
            return false;
        }
        skip_ws(json, i);

        std::string parsed_value;
        if (i < json.size() && json[i] == '"') {
            if (!parse_string(json, i, parsed_value)) {
                return false;
            }
        } else {
            size_t start = i;
            while (i < json.size() && json[i] != ',' && json[i] != '}') {
                ++i;
            }
            parsed_value = json.substr(start, i - start);
        }

        if (parsed_key == key) {
            value = parsed_value;
            return true;
        }
        skip_ws(json, i);
        if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
        }
        if (i < json.size() && json[i] == '}') {
            return false;
        }
    }
    return false;
}

}  // namespace chat
