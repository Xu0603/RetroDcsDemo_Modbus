#include "manager/modbus/modbus_codec.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}


// ============================================================
// 测试 Register Count
// ============================================================

void testRegisterCount() {
    using namespace demo;

    require(
        modbusRegisterCount(DataType::Bool) == 1,
        "BOOL register count failed"
    );

    require(
        modbusRegisterCount(DataType::Int16) == 1,
        "INT16 register count failed"
    );

    require(
        modbusRegisterCount(DataType::UInt16) == 1,
        "UINT16 register count failed"
    );

    require(
        modbusRegisterCount(DataType::Int32) == 2,
        "INT32 register count failed"
    );

    require(
        modbusRegisterCount(DataType::UInt32) == 2,
        "UINT32 register count failed"
    );

    require(
        modbusRegisterCount(DataType::Float32) == 2,
        "FLOAT32 register count failed"
    );

    require(
        modbusRegisterCount(DataType::Int64) == 4,
        "INT64 register count failed"
    );

    require(
        modbusRegisterCount(DataType::UInt64) == 4,
        "UINT64 register count failed"
    );

    require(
        modbusRegisterCount(DataType::Float64) == 4,
        "FLOAT64 register count failed"
    );

    std::cout << "[PASS] Register count test\n";
}


// ============================================================
// 测试 ByteOrder Parse
// ============================================================

void testByteOrderParse() {
    using namespace demo;

    require(
        parseModbusByteOrder("ABCD") ==
            ModbusByteOrder::ABCD,
        "parse ABCD failed"
    );

    require(
        parseModbusByteOrder("BADC") ==
            ModbusByteOrder::BADC,
        "parse BADC failed"
    );

    require(
        parseModbusByteOrder("CDAB") ==
            ModbusByteOrder::CDAB,
        "parse CDAB failed"
    );

    require(
        parseModbusByteOrder("DCBA") ==
            ModbusByteOrder::DCBA,
        "parse DCBA failed"
    );

    bool thrown = false;

    try {
        parseModbusByteOrder("INVALID");
    } catch (const std::exception &) {
        thrown = true;
    }

    require(
        thrown,
        "invalid byte order should throw"
    );

    std::cout << "[PASS] Byte order parse test\n";
}


// ============================================================
// 测试 BOOL
// ============================================================

void testBool() {
    using namespace demo;

    {
        std::vector<std::uint16_t> registers = {
            0
        };

        auto value = decodeModbusRegisters(
            registers,
            DataType::Bool,
            ModbusByteOrder::ABCD
        );

        require(
            std::get<bool>(value) == false,
            "BOOL false decode failed"
        );
    }

    {
        std::vector<std::uint16_t> registers = {
            1
        };

        auto value = decodeModbusRegisters(
            registers,
            DataType::Bool,
            ModbusByteOrder::ABCD
        );

        require(
            std::get<bool>(value) == true,
            "BOOL true decode failed"
        );
    }

    std::cout << "[PASS] BOOL decode test\n";
}


// ============================================================
// 测试 INT16
// ============================================================

void testInt16() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0xFFFE
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::Int16,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::int16_t>(value) == -2,
        "INT16 decode failed"
    );

    std::cout << "[PASS] INT16 decode test\n";
}


// ============================================================
// 测试 UINT16
// ============================================================

void testUInt16() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x1234
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt16,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::uint16_t>(value) == 0x1234,
        "UINT16 decode failed"
    );

    std::cout << "[PASS] UINT16 decode test\n";
}


// ============================================================
// 测试 INT32
//
// ABCD:
//
// Register:
// 0x0000
// 0x0001
//
// Result:
// 1
// ============================================================

void testInt32() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x0000,
        0x0001
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::Int32,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::int32_t>(value) == 1,
        "INT32 decode failed"
    );

    std::cout << "[PASS] INT32 decode test\n";
}


// ============================================================
// 测试 UINT32
// ============================================================

void testUInt32() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x1234,
        0x5678
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt32,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::uint32_t>(value) ==
            0x12345678,
        "UINT32 decode failed"
    );

    std::cout << "[PASS] UINT32 decode test\n";
}


// ============================================================
// 测试 INT64
// ============================================================

void testInt64() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x0000,
        0x0000,
        0x0000,
        0x0001
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::Int64,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::int64_t>(value) == 1,
        "INT64 decode failed"
    );

    std::cout << "[PASS] INT64 decode test\n";
}


// ============================================================
// 测试 UINT64
// ============================================================

void testUInt64() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x1234,
        0x5678,
        0x9ABC,
        0xDEF0
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt64,
        ModbusByteOrder::ABCD
    );

    require(
        std::get<std::uint64_t>(value) ==
            0x123456789ABCDEF0ULL,
        "UINT64 decode failed"
    );

    std::cout << "[PASS] UINT64 decode test\n";
}


// ============================================================
// 测试 FLOAT32
//
// IEEE754:
//
// 1.0f = 0x3F800000
//
// Registers:
//
// 0x3F80
// 0x0000
// ============================================================

void testFloat32() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x3F80,
        0x0000
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::Float32,
        ModbusByteOrder::ABCD
    );

    require(
        std::fabs(
            std::get<float>(value) - 1.0f
        ) < 1e-6f,
        "FLOAT32 decode failed"
    );

    std::cout << "[PASS] FLOAT32 decode test\n";
}


// ============================================================
// 测试 FLOAT64
//
// IEEE754:
//
// 1.0 =
//
// 0x3FF0000000000000
//
// Registers:
//
// 0x3FF0
// 0x0000
// 0x0000
// 0x0000
// ============================================================

void testFloat64() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x3FF0,
        0x0000,
        0x0000,
        0x0000
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::Float64,
        ModbusByteOrder::ABCD
    );

    require(
        std::fabs(
            std::get<double>(value) - 1.0
        ) < 1e-12,
        "FLOAT64 decode failed"
    );

    std::cout << "[PASS] FLOAT64 decode test\n";
}


// ============================================================
// 测试 BADC
//
// 原始寄存器:
//
// 0x3412
//
// BADC:
//
// bytes:
// 34 12
//
// swap:
//
// 12 34
//
// Result:
//
// 0x1234
// ============================================================

void testByteOrderBADC16() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x3412
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt16,
        ModbusByteOrder::BADC
    );

    require(
        std::get<std::uint16_t>(value) ==
            0x1234,
        "BADC UINT16 decode failed"
    );

    std::cout << "[PASS] BADC UINT16 test\n";
}


// ============================================================
// 测试 CDAB
//
// 原始 Registers:
//
// 0x5678
// 0x1234
//
// Bytes:
//
// 56 78 12 34
//
// CDAB:
//
// 12 34 56 78
//
// Result:
//
// 0x12345678
// ============================================================

void testByteOrderCDAB32() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x5678,
        0x1234
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt32,
        ModbusByteOrder::CDAB
    );

    require(
        std::get<std::uint32_t>(value) ==
            0x12345678,
        "CDAB UINT32 decode failed"
    );

    std::cout << "[PASS] CDAB UINT32 test\n";
}


// ============================================================
// 测试 DCBA
//
// 原始 Registers:
//
// 0x7856
// 0x3412
//
// Bytes:
//
// 78 56 34 12
//
// reverse:
//
// 12 34 56 78
//
// Result:
//
// 0x12345678
// ============================================================

void testByteOrderDCBA32() {
    using namespace demo;

    std::vector<std::uint16_t> registers = {
        0x7856,
        0x3412
    };

    auto value = decodeModbusRegisters(
        registers,
        DataType::UInt32,
        ModbusByteOrder::DCBA
    );

    require(
        std::get<std::uint32_t>(value) ==
            0x12345678,
        "DCBA UINT32 decode failed"
    );

    std::cout << "[PASS] DCBA UINT32 test\n";
}


// ============================================================
// 测试错误寄存器数量
// ============================================================

void testInvalidRegisterCount() {
    using namespace demo;

    bool thrown = false;

    try {

        std::vector<std::uint16_t> registers = {
            0x0001
        };

        decodeModbusRegisters(
            registers,
            DataType::UInt32,
            ModbusByteOrder::ABCD
        );

    } catch (const std::exception &) {

        thrown = true;
    }

    require(
        thrown,
        "invalid register count should throw"
    );

    std::cout << "[PASS] Invalid register count test\n";
}

void testEncodeRoundTrip() {
    using namespace demo;

    const std::vector<std::pair<DataType, Value>> values = {
        {DataType::Bool, true},
        {DataType::Int16, std::int16_t{-1234}},
        {DataType::UInt16, std::uint16_t{0xABCD}},
        {DataType::Int32, std::int32_t{-12345678}},
        {DataType::UInt32, std::uint32_t{0x12345678}},
        {DataType::Int64, std::int64_t{-1234567890123LL}},
        {DataType::UInt64, std::uint64_t{0x123456789ABCDEF0ULL}},
        {DataType::Float32, 123.25F},
        {DataType::Float64, -9876.125}
    };
    const std::vector<ModbusByteOrder> orders = {
        ModbusByteOrder::ABCD,
        ModbusByteOrder::BADC,
        ModbusByteOrder::CDAB,
        ModbusByteOrder::DCBA
    };

    for (const auto &[type, expected] : values) {
        for (const auto order : orders) {
            const auto registers = encodeModbusRegisters(expected, type, order);
            require(registers.size() == modbusRegisterCount(type),
                    "encoded register count mismatch");
            const auto actual = decodeModbusRegisters(registers, type, order);
            require(actual == expected, "Modbus encode/decode round trip failed");
        }
    }

    const auto registers = encodeModbusRegisters(
        Value{std::uint32_t{0x12345678}},
        DataType::UInt32,
        ModbusByteOrder::ABCD);
    require(registers == std::vector<std::uint16_t>({0x1234, 0x5678}),
            "ABCD UINT32 encode failed");

    std::cout << "[PASS] Encode round trip test\n";
}

} // namespace


// ============================================================
// Main
// ============================================================

int main() {
    try {

        std::cout
            << "Running Modbus codec tests...\n\n";

        testRegisterCount();

        testByteOrderParse();

        testBool();

        testInt16();

        testUInt16();

        testInt32();

        testUInt32();

        testInt64();

        testUInt64();

        testFloat32();

        testFloat64();

        testByteOrderBADC16();

        testByteOrderCDAB32();

        testByteOrderDCBA32();

        testInvalidRegisterCount();

        testEncodeRoundTrip();

        std::cout
            << "\nAll Modbus codec tests passed\n";

        return 0;

    } catch (const std::exception &e) {

        std::cerr
            << "\nModbus codec tests failed: "
            << e.what()
            << '\n';

        return 1;
    }
}
