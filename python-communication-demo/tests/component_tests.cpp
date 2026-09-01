#include "manager/config_repository.hpp"
#include "manager/ipc_protocol.hpp"
#include "manager/shared_memory.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char **argv) {
    using namespace demo;

    try {
        if (argc != 2) {
            throw std::runtime_error("database path required");
        }

        // ============================================================
        // 1. 测试 ConfigRepository
        // ============================================================

        ConfigRepository repository(argv[1]);

        repository.open();

        const auto config = repository.loadPublished();

        require(
            config.versionNo == 1,
            "wrong config version");

        require(
            config.points.size() == 3,
            "wrong point count");

        require(
            config.tasks.size() == 1,
            "wrong task count");

        require(
            config.tasks[0].bindings.size() == 3,
            "wrong binding count");


        // ============================================================
        // 2. 测试原 FLOAT64 SharedMemory
        // ============================================================

        const std::string shmName =
            "/pycom_component_" +
            std::to_string(getpid());

        SharedMemoryManager shm(
            shmName,
            config.versionNo,
            config.points);

        shm.create();

        auto before = shm.read(3);

        require(
            std::abs(std::get<double>(before.value)) < 1e-12,
            "wrong initial value");

        shm.write(
            3,
            15.48,
            PointStatus::Good);

        auto after = shm.read(3);

        require(
            std::abs(
                std::get<double>(after.value) - 15.48
            ) < 1e-12,
            "shared memory FLOAT64 write failed");

        require(
            after.sequence > before.sequence &&
            after.sequence % 2 == 0,
            "invalid sequence");


        // ============================================================
        // 3. 测试 INT16 SharedMemory
        // ============================================================

        PointConfig int16Point;

        int16Point.id = 101;
        int16Point.systemPointId = 2001;
        int16Point.name = "test_int16";
        int16Point.dataType = DataType::Int16;
        int16Point.direction = "INPUT";
        int16Point.initialValue = std::int16_t{0};
        int16Point.enabled = true;


        const std::string int16ShmName =
            "/pycom_int16_" +
            std::to_string(getpid());

        SharedMemoryManager int16Shm(
            int16ShmName,
            1,
            std::vector<PointConfig>{int16Point});

        int16Shm.create();

        int16Shm.write(
            101,
            std::int16_t{-12345},
            PointStatus::Good);

        const auto int16Result =
            int16Shm.read(101);

        require(
            std::get<std::int16_t>(
                int16Result.value
            ) == -12345,
            "shared memory INT16 write/read failed");


        // ============================================================
        // 4. 测试 UINT16 SharedMemory
        // ============================================================

        PointConfig uint16Point;

        uint16Point.id = 102;
        uint16Point.systemPointId = 2002;
        uint16Point.name = "test_uint16";
        uint16Point.dataType = DataType::UInt16;
        uint16Point.direction = "INPUT";
        uint16Point.initialValue = std::uint16_t{0};
        uint16Point.enabled = true;


        const std::string uint16ShmName =
            "/pycom_uint16_" +
            std::to_string(getpid());

        SharedMemoryManager uint16Shm(
            uint16ShmName,
            1,
            std::vector<PointConfig>{uint16Point});

        uint16Shm.create();

        uint16Shm.write(
            102,
            std::uint16_t{54321},
            PointStatus::Good);

        const auto uint16Result =
            uint16Shm.read(102);

        require(
            std::get<std::uint16_t>(
                uint16Result.value
            ) == 54321,
            "shared memory UINT16 write/read failed");


        // ============================================================
        // 5. 测试 IPC Protocol
        // ============================================================

        Message original{
            {"type", "SCRIPT_LOG"},
            {"message", "中文=value\nnext"}
        };

        require(
            decodeMessage(
                encodeMessage(original)
            ) == original,
            "IPC codec roundtrip failed");


        std::cout << "component tests passed\n";

        return 0;

    } catch (const std::exception &e) {

        std::cerr
            << "component tests failed: "
            << e.what()
            << '\n';

        return 1;
    }
}