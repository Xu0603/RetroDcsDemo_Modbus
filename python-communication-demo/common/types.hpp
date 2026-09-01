#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace demo {

// ============================================================
// 数据类型定义
// ============================================================
//
// 原系统支持：
// BOOL
// INT32
// UINT32
// INT64
// UINT64
// FLOAT32
// FLOAT64
//
// 为了支持 Modbus，新增：
// INT16
// UINT16
//
// 注意：
// 后面的枚举值保持原来的值不变，避免破坏现有 Python 通信功能。
// INT16 和 UINT16 放在最后新增。
// ============================================================

enum class DataType : std::uint16_t {
    Bool = 1,

    Int32 = 2,
    UInt32 = 3,

    Int64 = 4,
    UInt64 = 5,

    Float32 = 6,
    Float64 = 7,

    // Modbus 新增数据类型
    Int16 = 8,
    UInt16 = 9,
};


// ============================================================
// 点状态
// ============================================================

enum class PointStatus : std::uint16_t {
    NotConnected = 0,
    Bad = 1,
    Good = 2,
};


// ============================================================
// 统一数据值类型
// ============================================================
//
// 所有进入 SystemManager SharedMemory 的数据
// 都通过 Value 进行表示。
//
// 新增：
// int16_t
// uint16_t
// ============================================================

using Value = std::variant<
    bool,

    std::int16_t,
    std::uint16_t,

    std::int32_t,
    std::uint32_t,

    std::int64_t,
    std::uint64_t,

    float,
    double
>;


// ============================================================
// DataType 转字符串
// ============================================================

inline std::string toString(DataType type)
{
    switch (type) {

    case DataType::Bool:
        return "BOOL";

    case DataType::Int16:
        return "INT16";

    case DataType::UInt16:
        return "UINT16";

    case DataType::Int32:
        return "INT32";

    case DataType::UInt32:
        return "UINT32";

    case DataType::Int64:
        return "INT64";

    case DataType::UInt64:
        return "UINT64";

    case DataType::Float32:
        return "FLOAT32";

    case DataType::Float64:
        return "FLOAT64";
    }

    return "UNKNOWN";
}


// ============================================================
// 字符串转 DataType
// ============================================================
//
// 用于：
// SQLite 配置读取
// JSON 配置
// UI 配置
// ============================================================

inline DataType parseDataType(const std::string &text)
{
    if (text == "BOOL")
        return DataType::Bool;

    // Modbus 16位类型
    if (text == "INT16")
        return DataType::Int16;

    if (text == "UINT16")
        return DataType::UInt16;

    // 32位类型
    if (text == "INT32")
        return DataType::Int32;

    if (text == "UINT32")
        return DataType::UInt32;

    // 64位类型
    if (text == "INT64")
        return DataType::Int64;

    if (text == "UINT64")
        return DataType::UInt64;

    // 浮点数
    if (text == "FLOAT32" || text == "FLOAT")
        return DataType::Float32;

    if (text == "FLOAT64" || text == "DOUBLE")
        return DataType::Float64;

    throw std::runtime_error(
        "unsupported data type: " + text
    );
}


// ============================================================
// Value 转字符串
// ============================================================

inline std::string valueToString(const Value &value)
{
    return std::visit(
        [](auto v)
        {
            using T = decltype(v);

            if constexpr (std::is_same_v<T, bool>)
            {
                return std::string(v ? "true" : "false");
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                return std::to_string(v);
            }
            else
            {
                return std::to_string(v);
            }
        },
        value
    );
}


// ============================================================
// 字符串转 Value
// ============================================================
//
// 用于：
// 配置初始值
// FORCE_WRITE
// UI 输入数据
// ============================================================

inline Value parseValue(
    DataType type,
    const std::string &text
)
{
    std::size_t used = 0;

    switch (type)
    {
    case DataType::Bool:
    {
        if (text == "true" || text == "1")
            return true;

        if (text == "false" || text == "0")
            return false;

        break;
    }


    // --------------------------------------------------------
    // INT16
    // --------------------------------------------------------

    case DataType::Int16:
    {
        const auto v = std::stoll(text, &used);

        if (used == text.size() &&
            v >= INT16_MIN &&
            v <= INT16_MAX)
        {
            return static_cast<std::int16_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // UINT16
    // --------------------------------------------------------

    case DataType::UInt16:
    {
        const auto v = std::stoull(text, &used);

        if (used == text.size() &&
            v <= UINT16_MAX)
        {
            return static_cast<std::uint16_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // INT32
    // --------------------------------------------------------

    case DataType::Int32:
    {
        const auto v = std::stoll(text, &used);

        if (used == text.size() &&
            v >= INT32_MIN &&
            v <= INT32_MAX)
        {
            return static_cast<std::int32_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // UINT32
    // --------------------------------------------------------

    case DataType::UInt32:
    {
        const auto v = std::stoull(text, &used);

        if (used == text.size() &&
            v <= UINT32_MAX)
        {
            return static_cast<std::uint32_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // INT64
    // --------------------------------------------------------

    case DataType::Int64:
    {
        const auto v = std::stoll(text, &used);

        if (used == text.size())
        {
            return static_cast<std::int64_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // UINT64
    // --------------------------------------------------------

    case DataType::UInt64:
    {
        const auto v = std::stoull(text, &used);

        if (used == text.size())
        {
            return static_cast<std::uint64_t>(v);
        }

        break;
    }


    // --------------------------------------------------------
    // FLOAT32
    // --------------------------------------------------------

    case DataType::Float32:
    {
        const auto v = std::stof(text, &used);

        if (used == text.size() &&
            std::isfinite(v))
        {
            return v;
        }

        break;
    }


    // --------------------------------------------------------
    // FLOAT64
    // --------------------------------------------------------

    case DataType::Float64:
    {
        const auto v = std::stod(text, &used);

        if (used == text.size() &&
            std::isfinite(v))
        {
            return v;
        }

        break;
    }
    }

    throw std::runtime_error(
        "invalid value '" +
        text +
        "' for " +
        toString(type)
    );
}


// ============================================================
// 系统点配置
// ============================================================
//
// Point 是整个 SystemManager 的统一数据点。
// 不管数据来自：
// Python
// Modbus
// OPC UA（未来）
// MQTT（未来）
//
// 最终都对应一个 Point。
// ============================================================

struct PointConfig
{
    std::uint32_t id{};

    std::uint32_t systemPointId{};

    std::string name;

    DataType dataType{};

    std::string direction;

    Value initialValue{};

    bool enabled{};
};


// ============================================================
// Python Binding
// ============================================================
//
// 保持原来的 BindingConfig 不变。
// 它表示 Python Task 和 Point 的绑定关系。
// ============================================================

struct BindingConfig
{
    std::uint32_t pointId{};

    std::string alias;

    std::string accessMode;

    double deadband{};
};


// ============================================================
// Python Task
// ============================================================
//
// 保持原来的 Python Task 结构。
// ============================================================

struct TaskConfig
{
    std::uint32_t id{};

    std::string name;

    std::string scriptPath;

    std::string entryFunction;

    std::string triggerType;

    std::uint32_t intervalMs{};

    std::uint32_t timeoutMs{};

    std::string restartPolicy;

    bool enabled{};

    std::vector<BindingConfig> bindings;
};


// ============================================================
// Modbus 字节序
// ============================================================
//
// ABCD : 标准大端
// BADC : 字节交换
// CDAB : Word交换
// DCBA : 完全反转
//
// 用于：
// INT32
// UINT32
// FLOAT32
// INT64
// UINT64
// FLOAT64
// ============================================================

enum class ModbusByteOrder : std::uint8_t
{
    ABCD = 0,

    BADC = 1,

    CDAB = 2,

    DCBA = 3
};


// ============================================================
// Modbus Binding
// ============================================================
//
// 表示：
// 一个 System Point
// 对应 Modbus 设备中的哪个地址。
//
// 例如：
//
// Point:
//     Temperature
//
// Modbus:
//     FC03
//     Address 0
//     FLOAT32
//     ABCD
//     1000ms
// ============================================================

struct ModbusBindingConfig
{
    // 对应系统 Point ID
    std::uint32_t pointId{};

    // Modbus 功能码
    //
    // 1  Read Coils
    // 2  Read Discrete Inputs
    // 3  Read Holding Registers
    // 4  Read Input Registers
    //
    std::uint8_t functionCode{};

    // Modbus 地址
    std::uint32_t address{};

    // 数据类型
    DataType dataType{};

    // 字节序
    ModbusByteOrder byteOrder{
        ModbusByteOrder::ABCD
    };

    // 采集周期（毫秒）
    std::uint32_t periodMs{1000};

    // 是否启用
    bool enabled{true};
};


// ============================================================
// Modbus Device
// ============================================================
//
// 表示一个 Modbus TCP 通信设备。
//
// 例如：
//
// PLC01
// 127.0.0.1
// Port: 502
// Slave ID: 1
// ============================================================

struct ModbusDeviceConfig
{
    // 数据库设备 ID
    std::uint32_t id{};

    // 设备名称
    std::string name;

    // 通信协议
    //
    // 第一阶段固定 Modbus TCP
    //
    std::string protocol{"TCP"};

    // Modbus TCP IP 地址
    std::string host;

    // Modbus TCP 端口
    std::uint16_t port{502};

    // Slave ID / Unit ID
    std::uint8_t slaveId{1};

    // 通信超时时间
    std::uint32_t timeoutMs{1000};

    // 断线重连周期
    std::uint32_t reconnectIntervalMs{3000};

    // 是否启用
    bool enabled{true};

    // 当前设备下面的所有 Modbus 点
    std::vector<ModbusBindingConfig> bindings;
};


// ============================================================
// RuntimeConfig
// ============================================================
//
// SystemManager 启动时加载的完整运行配置。
// ============================================================

struct RuntimeConfig
{
    std::uint32_t versionId{};

    std::uint32_t versionNo{};

    std::string contentHash;

    // 系统统一数据点
    std::vector<PointConfig> points;

    // Python 通信任务
    std::vector<TaskConfig> tasks;

    // Modbus 通信设备
    std::vector<ModbusDeviceConfig> modbusDevices;
};


} // namespace demo