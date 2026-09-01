#include "manager/ipc_protocol.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace demo {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

std::string percentEncode(const std::string &text) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/' || c == ' ') out += c;
        else {
            out += '%'; out += hex[c >> 4]; out += hex[c & 0x0f];
        }
    }
    return out;
}

std::string percentDecode(const std::string &text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hexValue(text[i + 1]);
            const int lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) { out += static_cast<char>((hi << 4) | lo); i += 2; continue; }
        }
        out += text[i];
    }
    return out;
}

std::string encodeMessage(const Message &message) {
    std::string payload;
    for (const auto &[key, value] : message)
        payload += percentEncode(key) + "=" + percentEncode(value) + "\n";
    return payload;
}

Message decodeMessage(const std::string &payload) {
    Message message;
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto pos = line.find('=');
        if (pos == std::string::npos) throw std::runtime_error("malformed IPC field");
        message[percentDecode(line.substr(0, pos))] = percentDecode(line.substr(pos + 1));
    }
    return message;
}

} // namespace demo
