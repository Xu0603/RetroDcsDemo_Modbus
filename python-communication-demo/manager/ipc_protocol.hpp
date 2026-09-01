#pragma once

#include <map>
#include <string>

namespace demo {

using Message = std::map<std::string, std::string>;

std::string percentEncode(const std::string &text);
std::string percentDecode(const std::string &text);
std::string encodeMessage(const Message &message);
Message decodeMessage(const std::string &payload);

} // namespace demo
