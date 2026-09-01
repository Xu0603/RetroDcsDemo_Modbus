#include "manager/modbus/modbus_client.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <modbus/modbus.h>

namespace demo {

ModbusClient::ModbusClient(
    std::string host,
    int port,
    int slaveId,
    int timeoutMs
)
    : host_(std::move(host)),
      port_(port),
      slaveId_(slaveId),
      timeoutMs_(timeoutMs) {
}


ModbusClient::~ModbusClient() {
    disconnect();

    if (ctx_) {
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
}


bool ModbusClient::connect() {

    if (connected_) {
        return true;
    }

    if (!ctx_) {

        ctx_ = modbus_new_tcp(
            host_.c_str(),
            port_
        );

        if (!ctx_) {

            setError(
                "modbus_new_tcp failed"
            );

            return false;
        }

        if (modbus_set_slave(
                ctx_,
                slaveId_
            ) == -1) {

            setError(
                modbus_strerror(errno)
            );

            modbus_free(ctx_);

            ctx_ = nullptr;

            return false;
        }

        const std::uint32_t seconds =
            static_cast<std::uint32_t>(
                timeoutMs_ / 1000
            );

        const std::uint32_t microseconds =
            static_cast<std::uint32_t>(
                (timeoutMs_ % 1000) * 1000
            );

        modbus_set_response_timeout(
            ctx_,
            seconds,
            microseconds
        );
    }

    if (modbus_connect(ctx_) == -1) {

        setError(
            modbus_strerror(errno)
        );

        connected_ = false;

        return false;
    }

    connected_ = true;

    lastError_.clear();

    return true;
}


void ModbusClient::disconnect() {

    if (ctx_ && connected_) {

        modbus_close(ctx_);

        connected_ = false;
    }
}


bool ModbusClient::isConnected() const {
    return connected_;
}


std::vector<std::uint16_t>
ModbusClient::readRegisters(
    int functionCode,
    int address,
    int count
) {

    if (!connected_) {

        throw std::runtime_error(
            "Modbus client is not connected"
        );
    }

    if (count <= 0) {

        throw std::runtime_error(
            "invalid Modbus register count"
        );
    }

    std::vector<std::uint16_t> registers(
        static_cast<std::size_t>(count)
    );

    int result = -1;

    switch (functionCode) {

    case 3:

        result =
            modbus_read_registers(
                ctx_,
                address,
                count,
                registers.data()
            );

        break;


    case 4:

        result =
            modbus_read_input_registers(
                ctx_,
                address,
                count,
                registers.data()
            );

        break;


    default:

        throw std::runtime_error(
            "unsupported Modbus register function code: " +
            std::to_string(functionCode)
        );
    }

    if (result == -1) {

        setError(
            modbus_strerror(errno)
        );

        connected_ = false;

        throw std::runtime_error(
            "Modbus read failed: " +
            lastError_
        );
    }

    registers.resize(
        static_cast<std::size_t>(result)
    );

    return registers;
}


const std::string &
ModbusClient::lastError() const {

    return lastError_;
}


void ModbusClient::setError(
    const std::string &error
) {

    lastError_ = error;
}

} // namespace demo