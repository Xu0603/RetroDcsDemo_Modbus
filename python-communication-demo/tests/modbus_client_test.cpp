#include "manager/modbus/modbus_client.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    using namespace demo;

    try {

        // 默认连接本机 Modbus TCP Server
        std::string host = "127.0.0.1";
        int port = 502;
        int slaveId = 1;

        // 允许命令行修改：
        //
        // ./modbus_client_test 127.0.0.1 502 1
        //
        if (argc >= 2) {
            host = argv[1];
        }

        if (argc >= 3) {
            port = std::stoi(argv[2]);
        }

        if (argc >= 4) {
            slaveId = std::stoi(argv[3]);
        }

        std::cout
            << "Connecting to Modbus TCP "
            << host
            << ":"
            << port
            << " slave="
            << slaveId
            << "\n";

        ModbusClient client(
            host,
            port,
            slaveId,
            1000
        );


        // ============================================================
        // Connect
        // ============================================================

        if (!client.connect()) {

            throw std::runtime_error(
                "connect failed: " +
                client.lastError()
            );
        }

        std::cout
            << "Modbus connected successfully\n";


        // ============================================================
        // Read Holding Registers
        //
        // Function Code 03
        // Address 0
        // Count 4
        // ============================================================

        const auto registers =
            client.readRegisters(
                3,
                0,
                4
            );

        std::cout
            << "Read "
            << registers.size()
            << " registers\n";

        for (std::size_t i = 0;
             i < registers.size();
             ++i) {

            std::cout
                << "reg["
                << i
                << "] = "
                << registers[i]
                << " (0x"
                << std::hex
                << registers[i]
                << std::dec
                << ")\n";
        }


        client.disconnect();

        std::cout
            << "modbus client test passed\n";

        return 0;

    } catch (const std::exception &e) {

        std::cerr
            << "modbus client test failed: "
            << e.what()
            << "\n";

        return 1;
    }
}