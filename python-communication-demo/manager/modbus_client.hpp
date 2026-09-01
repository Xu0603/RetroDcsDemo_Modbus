#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct _modbus;

namespace demo {

// ============================================================
// Modbus Read Result
// ============================================================
//
// 一次 Modbus 读取的结果。
//
// success:
//     是否读取成功
//
// value:
//     解析后的系统 Value
//
// error:
//     错误信息
//
// ============================================================

struct ModbusReadResult {
bool success{false};

```
Value value{};

std::string error;
```

};

// ============================================================
// Modbus Client
// ============================================================
//
// 负责：
//
// 1. Modbus TCP 连接
// 2. Slave ID 设置
// 3. Function Code 调用
// 4. 原始寄存器读取
// 5. 数据类型转换
// 6. 字节序转换
//
// 不负责：
//
// - 定时采集
// - 线程管理
// - Shared Memory
// - SQLite
// - UI
//
// ============================================================

class ModbusClient {
public:

```
ModbusClient();

~ModbusClient();

ModbusClient(
    const ModbusClient &
) = delete;

ModbusClient &operator=(
    const ModbusClient &
) = delete;


// ========================================================
// Connect
// ========================================================
//
// 建立 Modbus TCP 连接
//
// host:
//     IP 地址
//
// port:
//     TCP 端口
//
// slaveId:
//     Modbus Unit ID
//
// timeoutMs:
//     通信超时时间
//
// ========================================================

void connect(
    const std::string &host,
    std::uint16_t port,
    std::uint8_t slaveId,
    std::uint32_t timeoutMs
);


// ========================================================
// Disconnect
// ========================================================

void disconnect();


// ========================================================
// Connection Status
// ========================================================

bool isConnected() const;


// ========================================================
// Read
// ========================================================
//
// 根据 ModbusBindingConfig 读取数据。
//
// 自动处理：
//
// FC01
// FC02
// FC03
// FC04
//
// BOOL
// INT16
// UINT16
// INT32
// UINT32
// INT64
// UINT64
// FLOAT32
// FLOAT64
//
// ========================================================

ModbusReadResult read(
    const ModbusBindingConfig &binding
);
```

private:

```
// ========================================================
// libmodbus Context
// ========================================================

_modbus *ctx_{nullptr};


// ========================================================
// Internal Read Functions
// ========================================================

ModbusReadResult readBits(
    const ModbusBindingConfig &binding
);

ModbusReadResult readRegisters(
    const ModbusBindingConfig &binding
);


// ========================================================
// Register Count
// ========================================================

static int registerCount(
    DataType type
);


// ========================================================
// Parse Register Data
// ========================================================

static Value parseRegisters(
    const std::vector<std::uint16_t> &registers,
    DataType type,
    ModbusByteOrder byteOrder
);


// ========================================================
// Byte Order Conversion
// ========================================================

static std::vector<std::uint8_t> registersToBytes(
    const std::vector<std::uint16_t> &registers
);

static void applyByteOrder(
    std::vector<std::uint8_t> &bytes,
    ModbusByteOrder byteOrder
);


// ========================================================
// Raw Integer Conversion
// ========================================================

static std::uint16_t readUint16(
    const std::vector<std::uint8_t> &bytes
);

static std::uint32_t readUint32(
    const std::vector<std::uint8_t> &bytes
);

static std::uint64_t readUint64(
    const std::vector<std::uint8_t> &bytes
);
```

};

} // namespace demo
