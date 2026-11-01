#include "manager/modbus/modbus_manager.hpp"

#include "manager/modbus/modbus_client.hpp"
#include "manager/modbus/modbus_codec.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace demo {

namespace {

// ============================================================
// 默认值
// ============================================================
//
// 当 Modbus 通信失败时，向 SharedMemory 写入默认值。
// 同时状态设置为 Bad。
// ============================================================

Value defaultValue(
    DataType type
)
{
    switch (type)
    {
    case DataType::Bool:
        return false;

    case DataType::Int16:
        return std::int16_t{0};

    case DataType::UInt16:
        return std::uint16_t{0};

    case DataType::Int32:
        return std::int32_t{0};

    case DataType::UInt32:
        return std::uint32_t{0};

    case DataType::Int64:
        return std::int64_t{0};

    case DataType::UInt64:
        return std::uint64_t{0};

    case DataType::Float32:
        return float{0};

    case DataType::Float64:
        return double{0};
    }

    return std::uint16_t{0};
}

} // namespace


// ============================================================
// Constructor
// ============================================================

ModbusManager::ModbusManager(
    const std::vector<ModbusDeviceConfig> &devices,
    const std::vector<PointConfig> &points,
    SharedMemoryManager &sharedMemory
)
    :
    sharedMemory_(sharedMemory)
{
    for (const auto &point : points)
        points_[point.id] = &point;

    for (
        const auto &device :
        devices
    )
    {
        if (!device.enabled)
            continue;

        auto runtime =
            std::make_unique<DeviceRuntime>();

        runtime->config =
            device;

        devices_.push_back(
            std::move(runtime)
        );
    }
}


// ============================================================
// Destructor
// ============================================================

ModbusManager::~ModbusManager()
{
    stop();
}


// ============================================================
// Start
// ============================================================

void ModbusManager::start()
{
    if (
        running_.exchange(true)
    )
    {
        return;
    }


    std::cout
        << "Starting ModbusManager"
        << std::endl;


    for (
        auto &device :
        devices_
    )
    {
        device->running =
            true;


        device->worker =
            std::thread(
                &ModbusManager::deviceLoop,
                this,
                std::ref(*device)
            );
    }
}


// ============================================================
// Stop
// ============================================================

void ModbusManager::stop()
{
    if (
        !running_.exchange(false)
    )
    {
        return;
    }


    std::cout
        << "Stopping ModbusManager"
        << std::endl;


    for (
        auto &device :
        devices_
    )
    {
        device->running =
            false;
    }


    for (
        auto &device :
        devices_
    )
    {
        if (
            device->worker.joinable()
        )
        {
            device->worker.join();
        }
    }
}


// ============================================================
// Is Running
// ============================================================

bool ModbusManager::isRunning() const
{
    return running_;
}


// ============================================================
// Device Loop
// ============================================================

void ModbusManager::deviceLoop(
    DeviceRuntime &device
)
{
    const auto &config =
        device.config;


    std::cout
        << "[Modbus] Starting device: "
        << config.name
        << " "
        << config.host
        << ":"
        << config.port
        << std::endl;


    ModbusClient client(
        config.host,
        config.port,
        config.slaveId,
        config.timeoutMs
    );


    while (
        running_ &&
        device.running
    )
    {
        // ====================================================
        // Connect
        // ====================================================

        if (
            !client.isConnected()
        )
        {
            std::cout
                << "[Modbus] Connecting device "
                << config.name
                << std::endl;


            if (
                !client.connect()
            )
            {
                std::cerr
                    << "[Modbus] Connection failed: "
                    << config.name
                    << " error="
                    << client.lastError()
                    << std::endl;


                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        config.reconnectIntervalMs
                    )
                );

                continue;
            }


            std::cout
                << "[Modbus] Connected: "
                << config.name
                << std::endl;
        }


        // ====================================================
        // Read Bindings
        // ====================================================

        for (
            const auto &binding :
            config.bindings
        )
        {
            if (
                !running_ ||
                !device.running
            )
            {
                break;
            }


            if (
                !binding.enabled
            )
            {
                continue;
            }

            const auto pointIt = points_.find(binding.pointId);
            if (pointIt == points_.end())
            {
                std::cerr << "[Modbus] Binding references unknown point="
                          << binding.pointId << std::endl;
                continue;
            }
            const auto &point = *pointIt->second;

            try
            {
                if (point.direction == "OUTPUT")
                {
                    const auto snapshot = sharedMemory_.read(binding.pointId);
                    if (binding.dataType == DataType::Bool)
                    {
                        client.writeBit(
                            static_cast<int>(binding.address),
                            std::get<bool>(snapshot.value));
                    }
                    else
                    {
                        const auto registers = encodeModbusRegisters(
                            snapshot.value, binding.dataType, binding.byteOrder);
                        client.writeRegisters(
                            static_cast<int>(binding.address), registers);
                    }

                    if (snapshot.status != PointStatus::Good)
                        sharedMemory_.write(
                            binding.pointId, snapshot.value, PointStatus::Good);

                    std::cout << "[Modbus] " << config.name
                              << " Output Point=" << binding.pointId
                              << " Address=" << binding.address
                              << " Value=" << valueToString(snapshot.value)
                              << std::endl;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(binding.periodMs));
                    continue;
                }

                // ------------------------------------------------
                // 当前阶段支持：
                //
                // FC03 Read Holding Registers
                // FC04 Read Input Registers
                // ------------------------------------------------

                if (
                    binding.functionCode != 3 &&
                    binding.functionCode != 4
                )
                {
                    std::cerr
                        << "[Modbus] Unsupported function code currently: "
                        << static_cast<int>(
                            binding.functionCode
                        )
                        << std::endl;

                    continue;
                }


                // ------------------------------------------------
                // Calculate Register Count
                // ------------------------------------------------

                const auto count =
                    modbusRegisterCount(
                        binding.dataType
                    );


                // ------------------------------------------------
                // Read Registers
                // ------------------------------------------------

                const auto registers =
                    client.readRegisters(
                        binding.functionCode,
                        static_cast<int>(
                            binding.address
                        ),
                        static_cast<int>(
                            count
                        )
                    );


                // ------------------------------------------------
                // Decode Registers
                // ------------------------------------------------

                const auto value =
                    decodeModbusRegisters(
                        registers,
                        binding.dataType,
                        binding.byteOrder
                    );


                // ------------------------------------------------
                // Write Shared Memory
                // ------------------------------------------------

                sharedMemory_.write(
                    binding.pointId,
                    value,
                    PointStatus::Good
                );


                // ------------------------------------------------
                // Debug Output
                // ------------------------------------------------

                std::cout
                    << "[Modbus] "
                    << config.name
                    << " Point="
                    << binding.pointId
                    << " Address="
                    << binding.address
                    << " Value="
                    << valueToString(
                        value
                    )
                    << std::endl;
            }
            catch (
                const std::exception &e
            )
            {
                std::cerr
                    << "[Modbus] "
                    << (point.direction == "OUTPUT" ? "Write" : "Read")
                    << " failed"
                    << " device="
                    << config.name
                    << " point="
                    << binding.pointId
                    << " error="
                    << e.what()
                    << std::endl;


                // ------------------------------------------------
                // Write Bad Status
                // ------------------------------------------------

                try
                {
                    if (point.direction == "OUTPUT")
                    {
                        const auto snapshot = sharedMemory_.read(binding.pointId);
                        sharedMemory_.write(
                            binding.pointId, snapshot.value, PointStatus::Bad);
                    }
                    else
                    {
                        sharedMemory_.write(
                            binding.pointId,
                            defaultValue(binding.dataType),
                            PointStatus::Bad);
                    }
                }
                catch (...)
                {
                    // Ignore status update failure
                }


                // 当前连接可能已经失效
                client.disconnect();
            }


            // ====================================================
            // Period
            // ====================================================

            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    binding.periodMs
                )
            );
        }
    }


    client.disconnect();


    std::cout
        << "[Modbus] Device stopped: "
        << config.name
        << std::endl;
}

} // namespace demo
