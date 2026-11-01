#include "manager/modbus/modbus_codec.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace demo {
namespace {

// ============================================================
// 将 Modbus Register 转换为原始字节
// ============================================================
//
// Modbus Register:
//
// 0x1234
//
// 默认转换:
//
// [0] = 0x12
// [1] = 0x34
//
// ============================================================

std::vector<std::uint8_t> registersToBytes(
    const std::vector<std::uint16_t> &registers
) {
    std::vector<std::uint8_t> bytes;

    bytes.reserve(registers.size() * 2);

    for (const auto reg : registers) {

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
// ByteOrder 转换
// ============================================================

void applyByteOrder(
    std::vector<std::uint8_t> &bytes,
    ModbusByteOrder order
) {
    const std::size_t size = bytes.size();

    if (size == 2) {

        if (order == ModbusByteOrder::BADC ||
            order == ModbusByteOrder::DCBA) {

            std::swap(
                bytes[0],
                bytes[1]
            );
        }

        return;
    }


    if (size == 4) {

        switch (order) {

        case ModbusByteOrder::ABCD:
            break;

        case ModbusByteOrder::BADC:

            std::swap(bytes[0], bytes[1]);
            std::swap(bytes[2], bytes[3]);

            break;

        case ModbusByteOrder::CDAB:

            std::swap(bytes[0], bytes[2]);
            std::swap(bytes[1], bytes[3]);

            break;

        case ModbusByteOrder::DCBA:

            std::reverse(
                bytes.begin(),
                bytes.end()
            );

            break;
        }

        return;
    }


    if (size == 8) {

        switch (order) {

        case ModbusByteOrder::ABCD:
            break;

        case ModbusByteOrder::BADC:

            std::swap(bytes[0], bytes[1]);
            std::swap(bytes[2], bytes[3]);
            std::swap(bytes[4], bytes[5]);
            std::swap(bytes[6], bytes[7]);

            break;

        case ModbusByteOrder::CDAB:

            std::swap(bytes[0], bytes[2]);
            std::swap(bytes[1], bytes[3]);

            std::swap(bytes[4], bytes[6]);
            std::swap(bytes[5], bytes[7]);

            break;

        case ModbusByteOrder::DCBA:

            std::reverse(
                bytes.begin(),
                bytes.end()
            );

            break;
        }

        return;
    }


    throw std::runtime_error(
        "unsupported Modbus byte size"
    );
}


// ============================================================
// Copy Bytes → Integer
// ============================================================

template<typename T>
T bytesToValue(
    const std::vector<std::uint8_t> &bytes
) {
    if (bytes.size() != sizeof(T)) {

        throw std::runtime_error(
            "invalid Modbus byte count"
        );
    }

    T value{};

    std::array<std::uint8_t, sizeof(T)> nativeBytes{};

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

    std::reverse_copy(
        bytes.begin(),
        bytes.end(),
        nativeBytes.begin()
    );

#else

    std::copy(
        bytes.begin(),
        bytes.end(),
        nativeBytes.begin()
    );

#endif

    std::memcpy(
        &value,
        nativeBytes.data(),
        sizeof(T)
    );

    return value;
}

template<typename T>
std::vector<std::uint8_t> valueToBytes(const T &value) {
    std::array<std::uint8_t, sizeof(T)> nativeBytes{};
    std::memcpy(nativeBytes.data(), &value, sizeof(T));

    std::vector<std::uint8_t> bytes(sizeof(T));
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::reverse_copy(nativeBytes.begin(), nativeBytes.end(), bytes.begin());
#else
    std::copy(nativeBytes.begin(), nativeBytes.end(), bytes.begin());
#endif
    return bytes;
}

} // namespace


// ============================================================
// Parse ByteOrder
// ============================================================

ModbusByteOrder parseModbusByteOrder(
    const std::string &text
) {
    if (text == "ABCD")
        return ModbusByteOrder::ABCD;

    if (text == "BADC")
        return ModbusByteOrder::BADC;

    if (text == "CDAB")
        return ModbusByteOrder::CDAB;

    if (text == "DCBA")
        return ModbusByteOrder::DCBA;

    throw std::runtime_error(
        "unsupported Modbus byte order: " + text
    );
}


std::string toString(
    ModbusByteOrder order
) {
    switch (order) {

    case ModbusByteOrder::ABCD:
        return "ABCD";

    case ModbusByteOrder::BADC:
        return "BADC";

    case ModbusByteOrder::CDAB:
        return "CDAB";

    case ModbusByteOrder::DCBA:
        return "DCBA";
    }

    throw std::runtime_error(
        "invalid Modbus byte order"
    );
}


// ============================================================
// Register Count
// ============================================================

std::size_t modbusRegisterCount(
    DataType type
) {
    switch (type) {

    case DataType::Bool:
        return 1;

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
    }

    throw std::runtime_error(
        "unsupported Modbus data type"
    );
}


// ============================================================
// Decode Registers
// ============================================================

Value decodeModbusRegisters(
    const std::vector<std::uint16_t> &registers,
    DataType type,
    ModbusByteOrder byteOrder
) {
    const auto requiredCount =
        modbusRegisterCount(type);

    if (registers.size() != requiredCount) {

        throw std::runtime_error(
            "Modbus register count mismatch"
        );
    }


    auto bytes =
        registersToBytes(registers);


    applyByteOrder(
        bytes,
        byteOrder
    );


    switch (type) {

    // --------------------------------------------------------
    // BOOL
    // --------------------------------------------------------

    case DataType::Bool:

        return registers[0] != 0;


    // --------------------------------------------------------
    // INT16
    // --------------------------------------------------------

    case DataType::Int16:

        return bytesToValue<std::int16_t>(
            bytes
        );


    // --------------------------------------------------------
    // UINT16
    // --------------------------------------------------------

    case DataType::UInt16:

        return bytesToValue<std::uint16_t>(
            bytes
        );


    // --------------------------------------------------------
    // INT32
    // --------------------------------------------------------

    case DataType::Int32:

        return bytesToValue<std::int32_t>(
            bytes
        );


    // --------------------------------------------------------
    // UINT32
    // --------------------------------------------------------

    case DataType::UInt32:

        return bytesToValue<std::uint32_t>(
            bytes
        );


    // --------------------------------------------------------
    // INT64
    // --------------------------------------------------------

    case DataType::Int64:

        return bytesToValue<std::int64_t>(
            bytes
        );


    // --------------------------------------------------------
    // UINT64
    // --------------------------------------------------------

    case DataType::UInt64:

        return bytesToValue<std::uint64_t>(
            bytes
        );


    // --------------------------------------------------------
    // FLOAT32
    // --------------------------------------------------------

    case DataType::Float32:

        return bytesToValue<float>(
            bytes
        );


    // --------------------------------------------------------
    // FLOAT64
    // --------------------------------------------------------

    case DataType::Float64:

        return bytesToValue<double>(
            bytes
        );
    }


    throw std::runtime_error(
        "unsupported Modbus data type"
    );
}


std::vector<std::uint16_t> encodeModbusRegisters(
    const Value &value,
    DataType type,
    ModbusByteOrder byteOrder
) {
    std::vector<std::uint8_t> bytes;

    switch (type) {
    case DataType::Bool:
        bytes = valueToBytes<std::uint16_t>(
            std::get<bool>(value) ? std::uint16_t{1} : std::uint16_t{0});
        break;
    case DataType::Int16:
        bytes = valueToBytes(std::get<std::int16_t>(value));
        break;
    case DataType::UInt16:
        bytes = valueToBytes(std::get<std::uint16_t>(value));
        break;
    case DataType::Int32:
        bytes = valueToBytes(std::get<std::int32_t>(value));
        break;
    case DataType::UInt32:
        bytes = valueToBytes(std::get<std::uint32_t>(value));
        break;
    case DataType::Int64:
        bytes = valueToBytes(std::get<std::int64_t>(value));
        break;
    case DataType::UInt64:
        bytes = valueToBytes(std::get<std::uint64_t>(value));
        break;
    case DataType::Float32:
        bytes = valueToBytes(std::get<float>(value));
        break;
    case DataType::Float64:
        bytes = valueToBytes(std::get<double>(value));
        break;
    }

    // applyByteOrder is self-inverse for all supported permutations.
    applyByteOrder(bytes, byteOrder);

    std::vector<std::uint16_t> registers;
    registers.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        registers.push_back(static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[i]) << 8) |
            static_cast<std::uint16_t>(bytes[i + 1])));
    }
    return registers;
}

} // namespace demo
