#include "manager/modbus_client.hpp"

#include <modbus/modbus.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace demo {

// ============================================================
// Constructor
// ============================================================

ModbusClient::ModbusClient()
{
}

// ============================================================
// Destructor
// ============================================================

ModbusClient::~ModbusClient()
{
disconnect();
}

// ============================================================
// Connect
// ============================================================

void ModbusClient::connect(
const std::string &host,
std::uint16_t port,
std::uint8_t slaveId,
std::uint32_t timeoutMs
)
{
disconnect();


// --------------------------------------------------------
// Create Modbus TCP Context
// --------------------------------------------------------

ctx_ =
    modbus_new_tcp(
        host.c_str(),
        static_cast<int>(port)
    );


if (!ctx_)
{
    throw std::runtime_error(
        "failed to create Modbus TCP context"
    );
}


// --------------------------------------------------------
// Set Slave / Unit ID
// --------------------------------------------------------

if (
    modbus_set_slave(
        ctx_,
        static_cast<int>(slaveId)
    ) == -1
)
{
    const std::string error =
        modbus_strerror(errno);

    modbus_free(ctx_);
    ctx_ = nullptr;

    throw std::runtime_error(
        "failed to set Modbus slave id: " +
        error
    );
}


// --------------------------------------------------------
// Set Response Timeout
// --------------------------------------------------------

const std::uint32_t seconds =
    timeoutMs / 1000;

const std::uint32_t microseconds =
    (timeoutMs % 1000) * 1000;


if (
    modbus_set_response_timeout(
        ctx_,
        seconds,
        microseconds
    ) == -1
)
{
    const std::string error =
        modbus_strerror(errno);

    modbus_free(ctx_);
    ctx_ = nullptr;

    throw std::runtime_error(
        "failed to set Modbus timeout: " +
        error
    );
}


// --------------------------------------------------------
// Connect
// --------------------------------------------------------

if (
    modbus_connect(ctx_) == -1
)
{
    const std::string error =
        modbus_strerror(errno);

    modbus_free(ctx_);
    ctx_ = nullptr;

    throw std::runtime_error(
        "failed to connect Modbus TCP " +
        host +
        ":" +
        std::to_string(port) +
        " : " +
        error
    );
}


}

// ============================================================
// Disconnect
// ============================================================

void ModbusClient::disconnect()
{
if (!ctx_)
return;


modbus_close(ctx_);

modbus_free(ctx_);

ctx_ = nullptr;


}

// ============================================================
// Connection Status
// ============================================================

bool ModbusClient::isConnected() const
{
return ctx_ != nullptr;
}

// ============================================================
// Read
// ============================================================

ModbusReadResult ModbusClient::read(
const ModbusBindingConfig &binding
)
{
if (!ctx_)
{
return {
false,
{},
"Modbus client is not connected"
};
}


try
{
    switch (
        binding.functionCode
    )
    {
    case 1:
    case 2:
        return readBits(binding);

    case 3:
    case 4:
        return readRegisters(binding);

    default:
        return {
            false,
            {},
            "unsupported Modbus function code"
        };
    }
}
catch (
    const std::exception &e
)
{
    return {
        false,
        {},
        e.what()
    };
}


}

// ============================================================
// Read Bits
// ============================================================
//
// FC01
// Read Coils
//
// FC02
// Read Discrete Inputs
//
// ============================================================

ModbusReadResult ModbusClient::readBits(
const ModbusBindingConfig &binding
)
{
if (
binding.dataType !=
DataType::Bool
)
{
return {
false,
{},
"bit function requires BOOL data type"
};
}


std::uint8_t bit{0};

int rc = -1;


// --------------------------------------------------------
// FC01
// --------------------------------------------------------

if (
    binding.functionCode == 1
)
{
    rc =
        modbus_read_bits(
            ctx_,
            static_cast<int>(
                binding.address
            ),
            1,
            &bit
        );
}


// --------------------------------------------------------
// FC02
// --------------------------------------------------------

else if (
    binding.functionCode == 2
)
{
    rc =
        modbus_read_input_bits(
            ctx_,
            static_cast<int>(
                binding.address
            ),
            1,
            &bit
        );
}


if (rc == -1)
{
    return {
        false,
        {},
        std::string(
            "Modbus bit read failed: "
        ) +
        modbus_strerror(errno)
    };
}


ModbusReadResult result;

result.success = true;

result.value =
    static_cast<bool>(
        bit != 0
    );

return result;


}

// ============================================================
// Read Registers
// ============================================================
//
// FC03
// Read Holding Registers
//
// FC04
// Read Input Registers
//
// ============================================================

ModbusReadResult ModbusClient::readRegisters(
const ModbusBindingConfig &binding
)
{
if (
binding.dataType ==
DataType::Bool
)
{
return {
false,
{},
"register function cannot use BOOL"
};
}


const int count =
    registerCount(
        binding.dataType
    );


if (count <= 0)
{
    return {
        false,
        {},
        "invalid register count"
    };
}


std::vector<std::uint16_t>
    registers(
        static_cast<std::size_t>(
            count
        )
    );


int rc = -1;


// --------------------------------------------------------
// FC03
// --------------------------------------------------------

if (
    binding.functionCode == 3
)
{
    rc =
        modbus_read_registers(
            ctx_,
            static_cast<int>(
                binding.address
            ),
            count,
            registers.data()
        );
}


// --------------------------------------------------------
// FC04
// --------------------------------------------------------

else if (
    binding.functionCode == 4
)
{
    rc =
        modbus_read_input_registers(
            ctx_,
            static_cast<int>(
                binding.address
            ),
            count,
            registers.data()
        );
}


if (rc == -1)
{
    return {
        false,
        {},
        std::string(
            "Modbus register read failed: "
        ) +
        modbus_strerror(errno)
    };
}


ModbusReadResult result;

result.success = true;

result.value =
    parseRegisters(
        registers,
        binding.dataType,
        binding.byteOrder
    );

return result;


}

// ============================================================
// Register Count
// ============================================================

int ModbusClient::registerCount(
DataType type
)
{
switch (type)
{
case DataType::Int16:
case DataType::UInt16:
return 1;


case DataType::Int32:
case DataType::UInt32:
case DataType::Float32:
    return 2;


case DataType::Int64:
case DataType::UInt64:
case DataType::Float64:
    return 4;


case DataType::Bool:
    return 0;
}


return 0;


}

// ============================================================
// Parse Registers
// ============================================================

Value ModbusClient::parseRegisters(
const std::vector[std::uint16_t](std::uint16_t) &registers,
DataType type,
ModbusByteOrder byteOrder
)
{
auto bytes =
registersToBytes(
registers
);


applyByteOrder(
    bytes,
    byteOrder
);


switch (type)
{
// --------------------------------------------------------
// INT16
// --------------------------------------------------------

case DataType::Int16:
{
    const auto raw =
        readUint16(bytes);

    return static_cast<std::int16_t>(
        raw
    );
}


// --------------------------------------------------------
// UINT16
// --------------------------------------------------------

case DataType::UInt16:
{
    return readUint16(bytes);
}


// --------------------------------------------------------
// INT32
// --------------------------------------------------------

case DataType::Int32:
{
    const auto raw =
        readUint32(bytes);

    return static_cast<std::int32_t>(
        raw
    );
}


// --------------------------------------------------------
// UINT32
// --------------------------------------------------------

case DataType::UInt32:
{
    return readUint32(bytes);
}


// --------------------------------------------------------
// INT64
// --------------------------------------------------------

case DataType::Int64:
{
    const auto raw =
        readUint64(bytes);

    return static_cast<std::int64_t>(
        raw
    );
}


// --------------------------------------------------------
// UINT64
// --------------------------------------------------------

case DataType::UInt64:
{
    return readUint64(bytes);
}


// --------------------------------------------------------
// FLOAT32
// --------------------------------------------------------

case DataType::Float32:
{
    const std::uint32_t raw =
        readUint32(bytes);

    float value{};

    std::memcpy(
        &value,
        &raw,
        sizeof(value)
    );

    return value;
}


// --------------------------------------------------------
// FLOAT64
// --------------------------------------------------------

case DataType::Float64:
{
    const std::uint64_t raw =
        readUint64(bytes);

    double value{};

    std::memcpy(
        &value,
        &raw,
        sizeof(value)
    );

    return value;
}


case DataType::Bool:
    break;
}


throw std::runtime_error(
    "unsupported Modbus data type"
);


}

// ============================================================
// Registers -> Bytes
// ============================================================
//
// Modbus Register:
//
// uint16_t 0x1234
//
// 转换:
//
// [0x12][0x34]
//
// 多个寄存器:
//
// 0x1122
// 0x3344
//
// 转换:
//
// [11][22][33][44]
//
// ============================================================

std::vector[std::uint8_t](std::uint8_t)
ModbusClient::registersToBytes(
const std::vector[std::uint16_t](std::uint16_t) &registers
)
{
std::vector[std::uint8_t](std::uint8_t) bytes;


bytes.reserve(
    registers.size() * 2
);


for (
    const auto reg :
    registers
)
{
    bytes.push_back(
        static_cast<std::uint8_t>(
            (reg >> 8) & 0xFF
        )
    );

    bytes.push_back(
        static_cast<std::uint8_t>(
            reg & 0xFF
        )
    );
}


return bytes;


}

// ============================================================
// Apply Byte Order
// ============================================================
//
// ABCD
//
// [A][B][C][D]
//
// BADC
//
// [B][A][D][C]
//
// CDAB
//
// [C][D][A][B]
//
// DCBA
//
// [D][C][B][A]
//
// ============================================================

void ModbusClient::applyByteOrder(
std::vector[std::uint8_t](std::uint8_t) &bytes,
ModbusByteOrder byteOrder
)
{
if (
bytes.size() == 2
)
{
// 对 16 位数据：
//
// ABCD / CDAB
// 视为正常
//
// BADC / DCBA
// 视为字节交换


    if (
        byteOrder ==
            ModbusByteOrder::BADC ||
        byteOrder ==
            ModbusByteOrder::DCBA
    )
    {
        std::swap(
            bytes[0],
            bytes[1]
        );
    }

    return;
}


if (
    bytes.size() == 4
)
{
    const auto original =
        bytes;


    switch (byteOrder)
    {
    case ModbusByteOrder::ABCD:

        bytes = {
            original[0],
            original[1],
            original[2],
            original[3]
        };

        break;


    case ModbusByteOrder::BADC:

        bytes = {
            original[1],
            original[0],
            original[3],
            original[2]
        };

        break;


    case ModbusByteOrder::CDAB:

        bytes = {
            original[2],
            original[3],
            original[0],
            original[1]
        };

        break;


    case ModbusByteOrder::DCBA:

        bytes = {
            original[3],
            original[2],
            original[1],
            original[0]
        };

        break;
    }

    return;
}


if (
    bytes.size() == 8
)
{
    const auto original =
        bytes;


    switch (byteOrder)
    {
    case ModbusByteOrder::ABCD:

        bytes = original;

        break;


    case ModbusByteOrder::BADC:

        bytes = {
            original[1],
            original[0],

            original[3],
            original[2],

            original[5],
            original[4],

            original[7],
            original[6]
        };

        break;


    case ModbusByteOrder::CDAB:

        bytes = {
            original[2],
            original[3],
            original[0],
            original[1],

            original[6],
            original[7],
            original[4],
            original[5]
        };

        break;


    case ModbusByteOrder::DCBA:

        std::reverse(
            bytes.begin(),
            bytes.end()
        );

        break;
    }
}


}

// ============================================================
// Read UINT16
// ============================================================

std::uint16_t ModbusClient::readUint16(
const std::vector[std::uint8_t](std::uint8_t) &bytes
)
{
if (
bytes.size() < 2
)
{
throw std::runtime_error(
"not enough bytes for UINT16"
);
}


return
    (static_cast<std::uint16_t>(
        bytes[0]
    ) << 8)
    |
    static_cast<std::uint16_t>(
        bytes[1]
    );


}

// ============================================================
// Read UINT32
// ============================================================

std::uint32_t ModbusClient::readUint32(
const std::vector[std::uint8_t](std::uint8_t) &bytes
)
{
if (
bytes.size() < 4
)
{
throw std::runtime_error(
"not enough bytes for UINT32"
);
}


std::uint32_t value = 0;


value |=
    static_cast<std::uint32_t>(
        bytes[0]
    ) << 24;

value |=
    static_cast<std::uint32_t>(
        bytes[1]
    ) << 16;

value |=
    static_cast<std::uint32_t>(
        bytes[2]
    ) << 8;

value |=
    static_cast<std::uint32_t>(
        bytes[3]
    );


return value;


}

// ============================================================
// Read UINT64
// ============================================================

std::uint64_t ModbusClient::readUint64(
const std::vector[std::uint8_t](std::uint8_t) &bytes
)
{
if (
bytes.size() < 8
)
{
throw std::runtime_error(
"not enough bytes for UINT64"
);
}


std::uint64_t value = 0;


for (
    int i = 0;
    i < 8;
    ++i
)
{
    value <<= 8;

    value |=
        static_cast<std::uint64_t>(
            bytes[
                static_cast<std::size_t>(
                    i
                )
            ]
        );
}


return value;


}

} // namespace demo
