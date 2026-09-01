#pragma once

#include "common/types.hpp"
#include "manager/shared_memory.hpp"
#include "manager/runtime_logger.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace demo {

class TcpQueryServer {
public:
    TcpQueryServer(std::string host, std::uint16_t port, std::uint32_t configVersion,
                   std::string configHash,
                   const std::vector<PointConfig> &points, SharedMemoryManager &sharedMemory,
                   RuntimeLogger &logger);
    ~TcpQueryServer();
    TcpQueryServer(const TcpQueryServer &) = delete;
    TcpQueryServer &operator=(const TcpQueryServer &) = delete;

    void start();
    void stop();

private:
    void serve();
    void handleClient(int clientFd);
    std::string pingJson() const;
    std::string snapshotJson() const;
    std::string forceWriteJson(const std::string &payload);

    std::string host_;
    std::uint16_t port_;
    std::uint32_t configVersion_;
    std::string configHash_;
    const std::vector<PointConfig> &points_;
    SharedMemoryManager &sharedMemory_;
    RuntimeLogger &logger_;
    std::atomic<bool> running_{false};
    int listenFd_{-1};
    std::thread thread_;
};

} // namespace demo
