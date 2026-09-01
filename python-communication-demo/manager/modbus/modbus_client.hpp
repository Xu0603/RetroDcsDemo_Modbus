#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct _modbus;
using modbus_t = struct _modbus;

namespace demo {

class ModbusClient {
public:
    ModbusClient(
        std::string host,
        int port,
        int slaveId,
        int timeoutMs
    );

    ~ModbusClient();

    ModbusClient(const ModbusClient &) = delete;
    ModbusClient &operator=(const ModbusClient &) = delete;

    bool connect();

    void disconnect();

    bool isConnected() const;

    std::vector<std::uint16_t> readRegisters(
        int functionCode,
        int address,
        int count
    );

    const std::string &lastError() const;

private:
    std::string host_;

    int port_{};

    int slaveId_{};

    int timeoutMs_{};

    modbus_t *ctx_{nullptr};

    bool connected_{false};

    std::string lastError_;

    void setError(const std::string &error);
};

} // namespace demo