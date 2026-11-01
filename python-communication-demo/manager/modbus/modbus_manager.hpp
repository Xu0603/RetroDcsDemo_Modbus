#pragma once

#include "common/types.hpp"
#include "manager/shared_memory.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace demo {

class ModbusManager
{
public:

    ModbusManager(
        const std::vector<ModbusDeviceConfig> &devices,
        const std::vector<PointConfig> &points,
        SharedMemoryManager &sharedMemory
    );

    ~ModbusManager();

    ModbusManager(
        const ModbusManager &
    ) = delete;

    ModbusManager &operator=(
        const ModbusManager &
    ) = delete;


    // 启动所有 Modbus 采集线程
    void start();


    // 停止所有 Modbus 采集线程
    void stop();


    bool isRunning() const;


private:

    struct DeviceRuntime
    {
        ModbusDeviceConfig config;

        std::thread worker;

        std::atomic<bool> running{
            false
        };
    };


    std::vector<
        std::unique_ptr<DeviceRuntime>
    > devices_;


    SharedMemoryManager &
        sharedMemory_;

    std::unordered_map<std::uint32_t, const PointConfig *> points_;


    std::atomic<bool>
        running_{
            false
        };


private:

    void deviceLoop(
        DeviceRuntime &device
    );

};

} // namespace demo
