#include "manager/shm_ipc.hpp"
#include "manager/shared_memory.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace demo {
namespace {

bool waitSemaphore(sem_t *sem, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        if (sem_trywait(sem) == 0) return true;
        if (errno != EAGAIN && errno != EINTR)
            throw std::runtime_error("sem_trywait failed: " + std::string(std::strerror(errno)));
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

sem_t *createSemaphore(const std::string &name, unsigned initialValue) {
    sem_unlink(name.c_str());
    sem_t *sem = sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, initialValue);
    if (sem == SEM_FAILED)
        throw std::runtime_error("sem_open failed for " + name + ": " + std::strerror(errno));
    return sem;
}

} // namespace

ShmIpcTransport::ShmIpcTransport(std::string name) : name_(std::move(name)) {
    if (name_.empty() || name_[0] != '/') name_ = "/" + name_;
}

ShmIpcTransport::~ShmIpcTransport() { closeAll(); }

std::string ShmIpcTransport::semaphoreName(const char *suffix) const {
    return name_ + suffix;
}

void ShmIpcTransport::create(const std::string &processEpoch) {
    shm_unlink(name_.c_str());
    fd_ = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd_ < 0) throw std::runtime_error("IPC shm_open failed: " + std::string(std::strerror(errno)));
    if (ftruncate(fd_, sizeof(IpcSharedLayout)) != 0)
        throw std::runtime_error("IPC ftruncate failed");
    void *mapping = mmap(nullptr, sizeof(IpcSharedLayout), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd_, 0);
    if (mapping == MAP_FAILED) throw std::runtime_error("IPC mmap failed");
    layout_ = static_cast<IpcSharedLayout *>(mapping);
    std::memset(layout_, 0, sizeof(*layout_));
    std::memcpy(layout_->magic, "PYCOMIPC", 8);
    layout_->abiVersion = kIpcAbiVersion;
    layout_->layoutSize = sizeof(IpcSharedLayout);
    layout_->capacity = kIpcQueueCapacity;
    layout_->slotPayload = kIpcSlotPayload;
    layout_->managerPid = getpid();
    layout_->managerHeartbeatNs = nowNs();
    std::strncpy(layout_->processEpoch, processEpoch.c_str(), sizeof(layout_->processEpoch) - 1);

    requestSlots_ = createSemaphore(semaphoreName("_qs"), kIpcQueueCapacity);
    requestItems_ = createSemaphore(semaphoreName("_qi"), 0);
    requestMutex_ = createSemaphore(semaphoreName("_qm"), 1);
    responseSlots_ = createSemaphore(semaphoreName("_ps"), kIpcQueueCapacity);
    responseItems_ = createSemaphore(semaphoreName("_pi"), 0);
    responseMutex_ = createSemaphore(semaphoreName("_pm"), 1);
}

bool ShmIpcTransport::push(IpcQueueMeta &meta, IpcSlot *slots, sem_t *slotsSem,
                           sem_t *itemsSem, sem_t *mutexSem,
                           const std::string &payload, int timeoutMs) {
    if (payload.empty() || payload.size() > kIpcSlotPayload) return false;
    if (!waitSemaphore(slotsSem, timeoutMs)) return false;
    if (!waitSemaphore(mutexSem, timeoutMs)) {
        sem_post(slotsSem);
        return false;
    }
    auto &slot = slots[meta.writeIndex % kIpcQueueCapacity];
    slot.length = static_cast<std::uint32_t>(payload.size());
    std::memcpy(slot.payload, payload.data(), payload.size());
    meta.writeIndex = (meta.writeIndex + 1) % kIpcQueueCapacity;
    __sync_synchronize();
    sem_post(mutexSem);
    sem_post(itemsSem);
    return true;
}

std::optional<std::string> ShmIpcTransport::pop(IpcQueueMeta &meta, IpcSlot *slots,
                                                 sem_t *slotsSem, sem_t *itemsSem,
                                                 sem_t *mutexSem, int timeoutMs) {
    if (!waitSemaphore(itemsSem, timeoutMs)) return std::nullopt;
    if (!waitSemaphore(mutexSem, timeoutMs)) {
        sem_post(itemsSem);
        return std::nullopt;
    }
    auto &slot = slots[meta.readIndex % kIpcQueueCapacity];
    if (slot.length == 0 || slot.length > kIpcSlotPayload) {
        sem_post(mutexSem);
        sem_post(slotsSem);
        throw std::runtime_error("invalid IPC shared-memory slot length");
    }
    std::string payload(slot.payload, slot.payload + slot.length);
    slot.length = 0;
    meta.readIndex = (meta.readIndex + 1) % kIpcQueueCapacity;
    __sync_synchronize();
    sem_post(mutexSem);
    sem_post(slotsSem);
    return payload;
}

bool ShmIpcTransport::sendToRunner(const Message &message, int timeoutMs) {
    return push(layout_->responses, layout_->responseSlots, responseSlots_,
                responseItems_, responseMutex_, encodeMessage(message), timeoutMs);
}

std::optional<Message> ShmIpcTransport::receiveFromRunner(int timeoutMs) {
    const auto payload = pop(layout_->requests, layout_->requestSlots, requestSlots_,
                             requestItems_, requestMutex_, timeoutMs);
    if (!payload) return std::nullopt;
    return decodeMessage(*payload);
}

void ShmIpcTransport::heartbeat() {
    if (layout_) __atomic_store_n(&layout_->managerHeartbeatNs, nowNs(), __ATOMIC_RELEASE);
}

void ShmIpcTransport::closeAll() {
    auto closeSem = [](sem_t *sem) { if (sem && sem != SEM_FAILED) sem_close(sem); };
    closeSem(requestSlots_); closeSem(requestItems_); closeSem(requestMutex_);
    closeSem(responseSlots_); closeSem(responseItems_); closeSem(responseMutex_);
    if (layout_) munmap(layout_, sizeof(IpcSharedLayout));
    if (fd_ >= 0) close(fd_);
    for (const char *suffix : {"_qs", "_qi", "_qm", "_ps", "_pi", "_pm"})
        sem_unlink(semaphoreName(suffix).c_str());
    if (!name_.empty()) shm_unlink(name_.c_str());
    layout_ = nullptr;
    fd_ = -1;
}

} // namespace demo
