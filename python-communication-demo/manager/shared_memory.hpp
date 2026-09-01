#pragma once

#include "common/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace demo {

constexpr std::uint32_t kShmAbiVersion = 1;

struct alignas(8) SharedHeader {
    char magic[8];
    std::uint32_t abiVersion;
    std::uint32_t headerSize;
    std::uint32_t recordSize;
    std::uint32_t pointCount;
    std::uint64_t configVersion;
    std::uint64_t generation;
    std::int64_t managerPid;
    std::int64_t managerHeartbeatNs;
    std::uint8_t reserved[64];
};

struct alignas(8) PointRecord {
    std::uint32_t pointId;
    std::uint16_t dataType;
    std::uint16_t status;
    std::uint64_t sequence;
    std::int64_t timestampNs;
    std::uint64_t valueBits;
    std::uint64_t reserved;
};

struct PointSnapshot {
    std::uint32_t pointId{};
    DataType dataType{};
    PointStatus status{};
    std::uint64_t sequence{};
    std::int64_t timestampNs{};
    Value value{};
};

class SharedMemoryManager {
public:
    SharedMemoryManager(std::string name, std::uint64_t configVersion,
                        const std::vector<PointConfig> &points);
    ~SharedMemoryManager();
    SharedMemoryManager(const SharedMemoryManager &) = delete;
    SharedMemoryManager &operator=(const SharedMemoryManager &) = delete;

    void create();
    void write(std::uint32_t pointId, const Value &value, PointStatus status);
    PointSnapshot read(std::uint32_t pointId) const;
    void heartbeat();
    std::vector<PointSnapshot> snapshot() const;
    const std::string &name() const { return name_; }

private:
    std::string name_;
    std::uint64_t configVersion_;
    std::vector<PointConfig> points_;
    std::unordered_map<std::uint32_t, std::size_t> index_;
    int fd_{-1};
    void *mapping_{nullptr};
    std::size_t mappingSize_{};
    SharedHeader *header_{nullptr};
    PointRecord *records_{nullptr};
    mutable std::mutex mutex_;
};

std::int64_t nowNs();

} // namespace demo
