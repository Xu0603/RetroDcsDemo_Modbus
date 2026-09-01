#pragma once

#include "manager/ipc_protocol.hpp"

#include <cstdint>
#include <optional>
#include <semaphore.h>
#include <string>

namespace demo {

constexpr std::uint32_t kIpcAbiVersion = 1;
constexpr std::uint32_t kIpcQueueCapacity = 64;
constexpr std::uint32_t kIpcSlotPayload = 4092;

struct IpcQueueMeta {
    std::uint32_t readIndex;
    std::uint32_t writeIndex;
};

struct IpcSlot {
    std::uint32_t length;
    char payload[kIpcSlotPayload];
};

struct IpcSharedLayout {
    char magic[8];
    std::uint32_t abiVersion;
    std::uint32_t layoutSize;
    std::uint32_t capacity;
    std::uint32_t slotPayload;
    std::int64_t managerPid;
    std::int64_t runnerPid;
    std::int64_t managerHeartbeatNs;
    std::int64_t runnerHeartbeatNs;
    char processEpoch[64];
    IpcQueueMeta requests;
    IpcQueueMeta responses;
    IpcSlot requestSlots[kIpcQueueCapacity];
    IpcSlot responseSlots[kIpcQueueCapacity];
};

class ShmIpcTransport {
public:
    explicit ShmIpcTransport(std::string name);
    ~ShmIpcTransport();
    ShmIpcTransport(const ShmIpcTransport &) = delete;
    ShmIpcTransport &operator=(const ShmIpcTransport &) = delete;

    void create(const std::string &processEpoch);
    bool sendToRunner(const Message &message, int timeoutMs = 1000);
    std::optional<Message> receiveFromRunner(int timeoutMs);
    void heartbeat();
    const std::string &name() const { return name_; }

private:
    bool push(IpcQueueMeta &meta, IpcSlot *slots, sem_t *slotsSem,
              sem_t *itemsSem, sem_t *mutexSem, const std::string &payload, int timeoutMs);
    std::optional<std::string> pop(IpcQueueMeta &meta, IpcSlot *slots,
                                   sem_t *slotsSem, sem_t *itemsSem,
                                   sem_t *mutexSem, int timeoutMs);
    void closeAll();
    std::string semaphoreName(const char *suffix) const;

    std::string name_;
    int fd_{-1};
    IpcSharedLayout *layout_{nullptr};
    sem_t *requestSlots_{nullptr};
    sem_t *requestItems_{nullptr};
    sem_t *requestMutex_{nullptr};
    sem_t *responseSlots_{nullptr};
    sem_t *responseItems_{nullptr};
    sem_t *responseMutex_{nullptr};
};

} // namespace demo
