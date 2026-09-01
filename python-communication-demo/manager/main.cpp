#include "manager/config_repository.hpp"
#include "manager/ipc_protocol.hpp"
#include "manager/runtime_logger.hpp"
#include "manager/shared_memory.hpp"
#include "manager/shm_ipc.hpp"
#include "manager/tcp_server.hpp"

// 新增
#include "manager/modbus/modbus_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace demo {
namespace {

std::atomic<bool> running{true};

void signalHandler(int) { running = false; }

struct Arguments {
    std::string database;
    std::string scriptsRoot;
    std::string shmName{ "/pycom_demo" };
    std::string logPath{ "/tmp/pycom-demo.log" };
    std::string python{ "python3" };
    std::string runner;
    std::string tcpHost{ "127.0.0.1" };
    std::uint16_t tcpPort{19090};
    std::string snapshotOut;
    int runSeconds{0};
    bool noRunner{false};
};

Arguments parseArguments(int argc, char **argv) {
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value after " + key);
            return argv[++i];
        };
        if (key == "--database") args.database = value();
        else if (key == "--scripts-root") args.scriptsRoot = value();
        else if (key == "--shm-name") args.shmName = value();
        else if (key == "--log") args.logPath = value();
        else if (key == "--python") args.python = value();
        else if (key == "--runner") args.runner = value();
        else if (key == "--tcp-host") args.tcpHost = value();
        else if (key == "--tcp-port") {
            const int port = std::stoi(value());
            if (port < 1 || port > 65535) throw std::runtime_error("invalid TCP port");
            args.tcpPort = static_cast<std::uint16_t>(port);
        }
        else if (key == "--snapshot-out") args.snapshotOut = value();
        else if (key == "--run-seconds") args.runSeconds = std::stoi(value());
        else if (key == "--no-runner") args.noRunner = true;
        else throw std::runtime_error("unknown argument: " + key);
    }
    if (args.database.empty() || args.scriptsRoot.empty())
        throw std::runtime_error("--database and --scripts-root are required");
    return args;
}

std::string statusName(PointStatus status) {
    switch (status) {
    case PointStatus::NotConnected: return "NotConnected";
    case PointStatus::Bad: return "Bad";
    case PointStatus::Good: return "Good";
    }
    return "Bad";
}

struct RunnerState {
    const TaskConfig *task{};
    pid_t pid{-1};
    std::string epoch;
    int failures{};
    std::chrono::steady_clock::time_point nextStart{};
    std::chrono::steady_clock::time_point lastHeartbeat{};
    bool connected{};
    std::shared_ptr<ShmIpcTransport> transport;
};

class ManagerRuntime {
public:
    ManagerRuntime(
    const Arguments &args,
    RuntimeConfig config,
    SharedMemoryManager &shm,
    RuntimeLogger &logger
)
    : args_(args),
      config_(std::move(config)),
      shm_(shm),
      logger_(logger),

      tcpServer_(
          args.tcpHost,
          args.tcpPort,
          config_.versionNo,
          config_.contentHash,
          config_.points,
          shm_,
          logger_
      ),

      modbusManager_(
          config_.modbusDevices,
          shm_
      )
{
        for (const auto &p : config_.points) points_[p.id] = &p;
        for (const auto &t : config_.tasks) {
            tasks_[t.id] = &t;
            RunnerState state;
            state.task = &t;
            state.nextStart = std::chrono::steady_clock::now();
            runners_[t.id] = state;
        }
    }

    void run() {

    // ========================================================
    // 启动 Modbus
    // ========================================================

    modbusManager_.start();

    logger_.log(
        "INFO",
        "modbus_started",
        "ModbusManager started"
    );


    // ========================================================
    // 启动 TCP Server
    // ========================================================

    tcpServer_.start();

    logger_.log(
        "INFO",
        "manager_started",
        "ipc=shared-memory+named-semaphores"
    );

    logger_.log(
        "INFO",
        "tcp_started",
        "listen=" + args_.tcpHost +
        ":" +
        std::to_string(args_.tcpPort)
    );
        const auto deadline = args_.runSeconds > 0
            ? std::chrono::steady_clock::now() + std::chrono::seconds(args_.runSeconds)
            : std::chrono::steady_clock::time_point::max();
        while (running && std::chrono::steady_clock::now() < deadline) {
            supervise();
            shm_.heartbeat();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        running = false;


// ========================================================
// 停止 Modbus
// ========================================================

modbusManager_.stop();

logger_.log(
    "INFO",
    "modbus_stopped",
    "ModbusManager stopped"
);


// ========================================================
// 停止 TCP Server
// ========================================================

tcpServer_.stop();


// ========================================================
// 停止 Python Runner
// ========================================================

stopRunners();


for (auto &thread : sessions_)
{
    if (thread.joinable())
        thread.join();
}


writeSnapshot();


logger_.log(
    "INFO",
    "manager_stopped",
    "normal shutdown"
);
    }

private:
    const BindingConfig *bindingFor(const TaskConfig &task, const std::string &alias) const {
        for (const auto &binding : task.bindings) if (binding.alias == alias) return &binding;
        return nullptr;
    }

    const PointConfig *pointFor(std::uint32_t id) const {
        const auto it = points_.find(id);
        return it == points_.end() ? nullptr : it->second;
    }

    Message baseReply(const Message &request, const std::string &type,
                      const std::string &code = "OK") const {
        Message result{{"type", type}, {"code", code},
                       {"request_id", request.count("request_id") ? request.at("request_id") : ""},
                       {"protocol_version", "1"},
                       {"config_version", std::to_string(config_.versionNo)}};
        return result;
    }

    void handleSession(std::uint32_t taskId, std::string epoch,
                       std::shared_ptr<ShmIpcTransport> transport) {
        try {
            const auto helloOpt = transport->receiveFromRunner(5000);
            if (!helloOpt) return; // Process exit is reported by supervise() with its real exit code.
            if (helloOpt->at("type") != "HELLO") throw std::runtime_error("first IPC message was not HELLO");
            const auto &hello = *helloOpt;
            taskId = static_cast<std::uint32_t>(std::stoul(hello.at("task_id")));
            epoch = hello.at("process_epoch");
            if (hello.at("protocol_version") != "1") {
                transport->sendToRunner(baseReply(hello, "HELLO_ACK", "VERSION_MISMATCH"));
                throw std::runtime_error("protocol version mismatch");
            }
            const auto taskIt = tasks_.find(taskId);
            if (taskIt == tasks_.end()) {
                transport->sendToRunner(baseReply(hello, "HELLO_ACK", "TASK_NOT_FOUND"));
                throw std::runtime_error("unknown task");
            }
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto &state = runners_.at(taskId);
                if (!args_.noRunner && state.epoch != epoch) {
                    transport->sendToRunner(baseReply(hello, "HELLO_ACK", "STALE_PROCESS"));
                    throw std::runtime_error("stale process epoch");
                }
                state.connected = true;
                state.lastHeartbeat = std::chrono::steady_clock::now();
            }
            transport->sendToRunner(baseReply(hello, "HELLO_ACK"));
            const auto &task = *taskIt->second;
            std::string bindings;
            for (const auto &b : task.bindings) {
                const auto *p = pointFor(b.pointId);
                if (!bindings.empty()) bindings += ";";
                bindings += b.alias + "|" + std::to_string(b.pointId) + "|" + toString(p->dataType)
                         + "|" + b.accessMode;
            }
            transport->sendToRunner({{"type", "TASK_CONFIG"}, {"task_id", std::to_string(task.id)},
                             {"config_version", std::to_string(config_.versionNo)},
                             {"script_path", task.scriptPath}, {"entry_function", task.entryFunction},
                             {"trigger_type", task.triggerType}, {"interval_ms", std::to_string(task.intervalMs)},
                             {"timeout_ms", std::to_string(task.timeoutMs)}, {"bindings", bindings}});
            logger_.log("INFO", "runner_connected", "task=" + std::to_string(taskId) + " epoch=" + epoch);

            auto lastMessage = std::chrono::steady_clock::now();
            std::set<std::string> subscriptions;
            while (running) {
                transport->heartbeat();
                const auto requestOpt = transport->receiveFromRunner(1000);
                if (!requestOpt) {
                    if (std::chrono::steady_clock::now() - lastMessage > std::chrono::seconds(4))
                        throw std::runtime_error("runner heartbeat timeout");
                    continue;
                }
                lastMessage = std::chrono::steady_clock::now();
                const auto &request = *requestOpt;
                if (request.at("process_epoch") != epoch)
                    throw std::runtime_error("process epoch changed");
                processMessage(*transport, task, request, subscriptions);
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto &state = runners_.at(taskId);
                state.lastHeartbeat = lastMessage;
            }
        } catch (const std::exception &e) {
            logger_.log("WARN", "runner_disconnected", "task=" + std::to_string(taskId) + " reason=" + e.what());
        }
        if (taskId) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            runners_.at(taskId).connected = false;
        }
    }

    void sendValueUpdate(ShmIpcTransport &transport, const BindingConfig &binding) {
        const auto snapshot = shm_.read(binding.pointId);
        transport.sendToRunner({{"type", "VALUE_UPDATE"}, {"alias", binding.alias},
                         {"data_type", toString(snapshot.dataType)},
                         {"value", valueToString(snapshot.value)},
                         {"status", statusName(snapshot.status)},
                         {"timestamp_ns", std::to_string(snapshot.timestampNs)},
                         {"sequence", std::to_string(snapshot.sequence)}});
    }

    void processMessage(ShmIpcTransport &transport, const TaskConfig &task, const Message &request,
                        std::set<std::string> &subscriptions) {
        const auto type = request.at("type");
        if (type == "HEARTBEAT") {
            transport.sendToRunner(baseReply(request, "HEARTBEAT_ACK"));
            return;
        }
        if (type == "SCRIPT_LOG") {
            // Script output is exposed by the Runner debug TCP service and shown in
            // the workstation packet monitor. Do not duplicate it in runtime.log.
            return;
        }
        if (type == "SCRIPT_EXCEPTION") {
            logger_.log("ERROR", "script_exception", "task=" + std::to_string(task.id) + " " + request.at("message"));
            return;
        }
        if (type == "EXECUTION_STAT") return;
        if (type == "SUBSCRIBE_REQUEST") {
            std::istringstream aliases(request.count("aliases") ? request.at("aliases") : "");
            std::string alias;
            auto reply = baseReply(request, "SUBSCRIBE_RESPONSE");
            while (std::getline(aliases, alias, ',')) {
                const auto *binding = bindingFor(task, alias);
                if (!binding || (binding->accessMode != "READ" && binding->accessMode != "READ_WRITE")) {
                    reply["code"] = binding ? "PERMISSION_DENIED" : "ALIAS_NOT_FOUND";
                    transport.sendToRunner(reply);
                    return;
                }
                subscriptions.insert(alias);
            }
            transport.sendToRunner(reply);
            for (const auto &subscribed : subscriptions)
                sendValueUpdate(transport, *bindingFor(task, subscribed));
            return;
        }
        if (type == "READ_REQUEST") {
            auto reply = baseReply(request, "READ_RESPONSE");
            const auto *binding = bindingFor(task, request.at("alias"));
            if (!binding || (binding->accessMode != "READ" && binding->accessMode != "READ_WRITE")) {
                reply["code"] = binding ? "PERMISSION_DENIED" : "ALIAS_NOT_FOUND";
                transport.sendToRunner(reply); return;
            }
            const auto snapshot = shm_.read(binding->pointId);
            reply["alias"] = binding->alias;
            reply["data_type"] = toString(snapshot.dataType);
            reply["value"] = valueToString(snapshot.value);
            reply["status"] = statusName(snapshot.status);
            reply["timestamp_ns"] = std::to_string(snapshot.timestampNs);
            reply["sequence"] = std::to_string(snapshot.sequence);
            transport.sendToRunner(reply); return;
        }
        if (type == "WRITE_REQUEST") {
            auto reply = baseReply(request, "WRITE_RESULT");
            const auto *binding = bindingFor(task, request.at("alias"));
            if (!binding || (binding->accessMode != "WRITE" && binding->accessMode != "READ_WRITE")) {
                reply["code"] = binding ? "PERMISSION_DENIED" : "ALIAS_NOT_FOUND";
                transport.sendToRunner(reply); return;
            }
            const auto *point = pointFor(binding->pointId);
            if (point->direction != "OUTPUT" && point->direction != "BIDIRECTIONAL") {
                reply["code"] = "PERMISSION_DENIED"; transport.sendToRunner(reply); return;
            }
            try {
                const auto value = parseValue(point->dataType, request.at("value"));
                shm_.write(point->id, value, PointStatus::Good);
            } catch (const std::exception &e) {
                reply["code"] = "TYPE_MISMATCH";
                reply["message"] = e.what();
            }
            transport.sendToRunner(reply);
            if (reply["code"] == "OK" && subscriptions.count(binding->alias))
                sendValueUpdate(transport, *binding);
            return;
        }
        auto reply = baseReply(request, "ERROR", "BAD_REQUEST");
        reply["message"] = "unsupported message type";
        transport.sendToRunner(reply);
    }

    void spawnRunner(RunnerState &state) {
        const auto now = std::chrono::steady_clock::now();
        state.epoch = std::to_string(state.task->id) + "-" + std::to_string(nowNs());
        const auto ipcName = "/pci" + std::to_string(state.task->id) + "_" +
                             std::to_string(static_cast<unsigned long long>(nowNs()) % 100000000ULL);
        auto transport = std::make_shared<ShmIpcTransport>(ipcName);
        transport->create(state.epoch);
        state.transport = transport;
        const auto runner = args_.runner.empty()
            ? (std::filesystem::path(args_.scriptsRoot).parent_path().parent_path()
               / "runner" / "runner.py").string()
            : args_.runner;
        if (!std::filesystem::is_regular_file(runner)) {
            logger_.log("ERROR", "runner_start_failed",
                        "task=" + std::to_string(state.task->id) + " runner_not_found=" + runner);
            ++state.failures;
            state.nextStart = now + std::chrono::seconds(std::min(30, 1 << std::min(state.failures - 1, 4)));
            return;
        }
        const auto taskId = std::to_string(state.task->id);
        const auto debugPort = std::to_string(20000 + (state.task->id % 40000));
        const pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("fork failed");
        if (pid == 0) {
            execlp(args_.python.c_str(), args_.python.c_str(), runner.c_str(),
                   "--ipc-name", ipcName.c_str(), "--task-id", taskId.c_str(),
                   "--epoch", state.epoch.c_str(), "--scripts-root", args_.scriptsRoot.c_str(),
                   "--debug-port", debugPort.c_str(),
                   static_cast<char *>(nullptr));
            _exit(127);
        }
        state.pid = pid;
        state.lastHeartbeat = now;
        sessions_.emplace_back([this, taskIdValue = state.task->id, epoch = state.epoch, transport] {
            handleSession(taskIdValue, epoch, transport);
        });
        logger_.log("INFO", "runner_started", "task=" + taskId + " pid=" + std::to_string(pid));
    }

    void supervise() {
        if (args_.noRunner) return;
        std::lock_guard<std::mutex> lock(stateMutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto &[id, state] : runners_) {
            if (!state.task->enabled) continue;
            if (state.pid > 0) {
                int status{};
                const auto result = waitpid(state.pid, &status, WNOHANG);
                if (result == state.pid) {
                    std::string reason;
                    if (WIFEXITED(status)) reason = "exit_code=" + std::to_string(WEXITSTATUS(status));
                    else if (WIFSIGNALED(status)) reason = "signal=" + std::to_string(WTERMSIG(status));
                    else reason = "wait_status=" + std::to_string(status);
                    logger_.log("WARN", "runner_exited", "task=" + std::to_string(id) + " " + reason);
                    state.pid = -1; state.connected = false; state.transport.reset(); ++state.failures;
                    const int delay = std::min(30, 1 << std::min(state.failures - 1, 4));
                    state.nextStart = now + std::chrono::seconds(delay);
                } else if (state.connected && now - state.lastHeartbeat > std::chrono::seconds(5)) {
                    logger_.log("ERROR", "runner_heartbeat_timeout", "task=" + std::to_string(id));
                    kill(state.pid, SIGTERM);
                }
            }
            if (state.pid < 0 && now >= state.nextStart && state.task->restartPolicy != "NEVER")
                spawnRunner(state);
        }
    }

    void stopRunners() {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto &[id, state] : runners_) if (state.pid > 0) kill(state.pid, SIGTERM);
        for (auto &[id, state] : runners_) if (state.pid > 0) {
            int status{};
            for (int i = 0; i < 20; ++i) {
                if (waitpid(state.pid, &status, WNOHANG) == state.pid) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (waitpid(state.pid, &status, WNOHANG) == 0) {
                kill(state.pid, SIGKILL);
                waitpid(state.pid, &status, 0);
            }
            state.pid = -1;
        }
    }

    void writeSnapshot() {
        if (args_.snapshotOut.empty()) return;
        std::ofstream out(args_.snapshotOut);
        out << "{\n  \"configVersion\": " << config_.versionNo << ",\n  \"points\": [\n";
        const auto values = shm_.snapshot();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto *point = pointFor(values[i].pointId);
            out << "    {\"id\":" << values[i].pointId << ",\"name\":\"" << point->name
                << "\",\"type\":\"" << toString(values[i].dataType) << "\",\"value\":"
                << valueToString(values[i].value) << ",\"status\":\"" << statusName(values[i].status)
                << "\",\"sequence\":" << values[i].sequence << "}";
            out << (i + 1 == values.size() ? "\n" : ",\n");
        }
        out << "  ]\n}\n";
    }

    Arguments args_;

RuntimeConfig config_;

SharedMemoryManager &shm_;

RuntimeLogger &logger_;


// ========================================================
// TCP 查询服务器
// ========================================================

TcpQueryServer tcpServer_;


// ========================================================
// Modbus 管理器
// ========================================================

ModbusManager modbusManager_;
    std::unordered_map<std::uint32_t, const PointConfig *> points_;
    std::unordered_map<std::uint32_t, const TaskConfig *> tasks_;
    std::map<std::uint32_t, RunnerState> runners_;
    std::mutex stateMutex_;
    std::vector<std::thread> sessions_;
};

} // namespace
} // namespace demo

int main(int argc, char **argv) {
    using namespace demo;
    try {
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        const auto args = parseArguments(argc, argv);
        ConfigRepository repository(args.database);
        repository.open();
        auto config = repository.loadPublished();
        ConfigRepository::validate(config, args.scriptsRoot);
        RuntimeLogger logger(args.logPath);
        SharedMemoryManager sharedMemory(args.shmName, config.versionNo, config.points);
        sharedMemory.create();
        ManagerRuntime runtime(args, std::move(config), sharedMemory, logger);
        runtime.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "system-managerd: " << e.what() << '\n';
        return 1;
    }
}
