#pragma once

#include "common/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace demo {

// 返回指定数据类型需要多少个 Modbus 寄存器
std::size_t modbusRegisterCount(DataType dataType);

// 将 Modbus 寄存器转换成字节数组，并按照指定字节序排列
std::vector<std::uint8_t> modbusRegistersToBytes(
    const std::vector<std::uint16_t> &registers,
    ModbusByteOrder byteOrder);

// 从 Modbus 寄存器解析为系统统一 Value
Value decodeModbusRegisters(
    const std::vector<std::uint16_t> &registers,
    DataType dataType,
    ModbusByteOrder byteOrder);

// 字符串转换为 ModbusByteOrder
ModbusByteOrder parseModbusByteOrder(const std::string &text);

} // namespace demo