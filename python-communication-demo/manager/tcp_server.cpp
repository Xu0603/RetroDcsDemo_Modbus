#include "manager/tcp_server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace demo {
namespace {

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint16_t kPing = 1;
constexpr std::uint16_t kSnapshot = 2;
constexpr std::uint16_t kForceWrite = 3;
constexpr std::uint32_t kMaxPayload = 1024 * 1024;

bool readAll(int fd, void *buffer, std::size_t length) {
    auto *p = static_cast<char *>(buffer);
    while (length) {
        const auto n = recv(fd, p, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return true;
}

bool writeAll(int fd, const void *buffer, std::size_t length) {
    const auto *p = static_cast<const char *>(buffer);
    while (length) {
        const auto n = send(fd, p, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return true;
}

std::string jsonEscape(const std::string &value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << '?';
            else out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string statusName(PointStatus status) {
    if (status == PointStatus::Good) return "Good";
    if (status == PointStatus::Bad) return "Bad";
    return "NotConnected";
}

void appendU16(std::string &data, std::uint16_t value) {
    value = htons(value);
    data.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

void appendU32(std::string &data, std::uint32_t value) {
    value = htonl(value);
    data.append(reinterpret_cast<const char *>(&value), sizeof(value));
}

std::optional<std::string> jsonStringField(const std::string &json, const std::string &key) {
    const std::string marker = "\"" + key + "\"";
    auto pos = json.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;
    std::string value;
    for (++pos; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (c == '"') return value;
        if (c == '\\' && pos + 1 < json.size()) {
            const char escaped = json[++pos];
            if (escaped == 'n') value += '\n';
            else if (escaped == 'r') value += '\r';
            else if (escaped == 't') value += '\t';
            else value += escaped;
        } else value += c;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> jsonUIntField(const std::string &json, const std::string &key) {
    const std::string marker = "\"" + key + "\"";
    auto pos = json.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    pos = json.find(':', pos + marker.size());
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    const auto end = json.find_first_not_of("0123456789", pos);
    if (end == pos) return std::nullopt;
    try {
        const auto value = std::stoull(json.substr(pos, end - pos));
        if (value > UINT32_MAX) return std::nullopt;
        return static_cast<std::uint32_t>(value);
    } catch (...) { return std::nullopt; }
}

} // namespace

TcpQueryServer::TcpQueryServer(std::string host, std::uint16_t port,
                               std::uint32_t configVersion,
                               std::string configHash,
                               const std::vector<PointConfig> &points,
                               SharedMemoryManager &sharedMemory, RuntimeLogger &logger)
    : host_(std::move(host)), port_(port), configVersion_(configVersion), configHash_(std::move(configHash)),
      points_(points), sharedMemory_(sharedMemory), logger_(logger) {}

TcpQueryServer::~TcpQueryServer() { stop(); }

void TcpQueryServer::start() {
    if (running_.exchange(true)) return;
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("TCP socket creation failed");
    int reuse = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1)
        throw std::runtime_error("invalid TCP host: " + host_);
    if (bind(listenFd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
        throw std::runtime_error("TCP bind failed " + host_ + ":" + std::to_string(port_) +
                                 ": " + std::strerror(errno));
    if (listen(listenFd_, 8) != 0) throw std::runtime_error("TCP listen failed");
    thread_ = std::thread([this] { serve(); });
}

void TcpQueryServer::stop() {
    running_ = false;
    if (listenFd_ >= 0) shutdown(listenFd_, SHUT_RDWR);
    if (thread_.joinable()) thread_.join();
    if (listenFd_ >= 0) close(listenFd_);
    listenFd_ = -1;
}

void TcpQueryServer::serve() {
    while (running_) {
        pollfd descriptor{listenFd_, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 200);
        if (ready <= 0) continue;
        const int client = accept(listenFd_, nullptr, nullptr);
        if (client < 0) continue;
        handleClient(client);
        close(client);
    }
}

void TcpQueryServer::handleClient(int clientFd) {
    timeval timeout{1, 0};
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    unsigned char header[16]{};
    if (!readAll(clientFd, header, sizeof(header))) return;
    if (std::memcmp(header, "PYSM", 4) != 0) return;
    std::uint16_t versionNet{}, typeNet{};
    std::uint32_t requestNet{}, lengthNet{};
    std::memcpy(&versionNet, header + 4, 2);
    std::memcpy(&typeNet, header + 6, 2);
    std::memcpy(&requestNet, header + 8, 4);
    std::memcpy(&lengthNet, header + 12, 4);
    const auto version = ntohs(versionNet);
    const auto type = ntohs(typeNet);
    const auto requestId = ntohl(requestNet);
    const auto length = ntohl(lengthNet);
    if (version != kProtocolVersion || length > kMaxPayload) return;
    std::string requestPayload(length, '\0');
    if (length && !readAll(clientFd, requestPayload.data(), length)) return;

    std::string payload;
    std::uint16_t responseType = static_cast<std::uint16_t>(type | 0x8000);
    if (type == kPing) payload = pingJson();
    else if (type == kSnapshot) payload = snapshotJson();
    else if (type == kForceWrite) payload = forceWriteJson(requestPayload);
    else {
        responseType = 0xffff;
        payload = "{\"error\":\"unsupported message type\"}";
    }
    std::string response("PYSM", 4);
    appendU16(response, kProtocolVersion);
    appendU16(response, responseType);
    appendU32(response, requestId);
    appendU32(response, static_cast<std::uint32_t>(payload.size()));
    response += payload;
    writeAll(clientFd, response.data(), response.size());
}

std::string TcpQueryServer::forceWriteJson(const std::string &payload) {
    const auto pointId = jsonUIntField(payload, "pointId");
    const auto valueText = jsonStringField(payload, "value");
    const auto configHash = jsonStringField(payload, "configHash");
    if (!pointId || !valueText || !configHash)
        return "{\"ok\":false,\"error\":\"BAD_REQUEST\"}";
    if (*configHash != configHash_)
        return "{\"ok\":false,\"error\":\"CONFIG_MISMATCH\"}";
    const auto point = std::find_if(points_.begin(), points_.end(),
        [pointId](const PointConfig &candidate) { return candidate.id == *pointId; });
    if (point == points_.end())
        return "{\"ok\":false,\"error\":\"POINT_NOT_FOUND\"}";
    try {
        const auto typedValue = parseValue(point->dataType, *valueText);
        sharedMemory_.write(point->id, typedValue, PointStatus::Good);
        logger_.log("WARN", "point_forced", "point=" + std::to_string(point->id) +
                    " name=" + point->name + " value=" + *valueText);
        return "{\"ok\":true,\"pointId\":" + std::to_string(point->id) +
               ",\"value\":\"" + jsonEscape(valueToString(typedValue)) + "\"}";
    } catch (const std::exception &error) {
        return "{\"ok\":false,\"error\":\"TYPE_MISMATCH\",\"message\":\"" +
               jsonEscape(error.what()) + "\"}";
    }
}

std::string TcpQueryServer::pingJson() const {
    return "{\"ok\":true,\"protocolVersion\":1,\"configVersion\":" +
           std::to_string(configVersion_) + ",\"configHash\":\"" + jsonEscape(configHash_) +
           "\",\"managerPid\":" +
           std::to_string(getpid()) + "}";
}

std::string TcpQueryServer::snapshotJson() const {
    const auto snapshots = sharedMemory_.snapshot();
    std::ostringstream out;
    out << "{\"ok\":true,\"protocolVersion\":1,\"configVersion\":"
        << configVersion_ << ",\"configHash\":\"" << jsonEscape(configHash_)
        << "\",\"managerPid\":" << getpid() << ",\"points\":[";
    for (std::size_t i = 0; i < snapshots.size(); ++i) {
        const auto &value = snapshots[i];
        const auto &point = points_[i];
        if (i) out << ',';
        out << "{\"id\":" << value.pointId
            << ",\"systemPointId\":" << point.systemPointId
            << ",\"name\":\"" << jsonEscape(point.name)
            << "\",\"dataType\":\"" << toString(value.dataType)
            << "\",\"direction\":\"" << jsonEscape(point.direction)
            << "\",\"value\":\"" << jsonEscape(valueToString(value.value))
            << "\",\"status\":\"" << statusName(value.status)
            << "\",\"timestampNs\":\"" << value.timestampNs
            << "\",\"sequence\":\"" << value.sequence << "\"}";
    }
    out << "]}";
    return out.str();
}

} // namespace demo
