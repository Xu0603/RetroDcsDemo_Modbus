#include "manager/shared_memory.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace demo {
namespace {

std::uint64_t encodeValue(const Value &value, DataType type) {
    std::uint64_t bits = 0;

    switch (type) {
    case DataType::Bool: {
        bits = std::get<bool>(value) ? 1 : 0;
        break;
    }

    case DataType::Int16: {
        auto v = std::get<std::int16_t>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::UInt16: {
        auto v = std::get<std::uint16_t>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::Int32: {
        auto v = std::get<std::int32_t>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::UInt32: {
        auto v = std::get<std::uint32_t>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::Int64: {
        auto v = std::get<std::int64_t>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::UInt64: {
        bits = std::get<std::uint64_t>(value);
        break;
    }

    case DataType::Float32: {
        auto v = std::get<float>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }

    case DataType::Float64: {
        auto v = std::get<double>(value);
        std::memcpy(&bits, &v, sizeof(v));
        break;
    }
    }

    return bits;
}

Value decodeValue(std::uint64_t bits, DataType type) {
    switch (type) {
    case DataType::Bool:
        return bits != 0;

    case DataType::Int16: {
        std::int16_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::UInt16: {
        std::uint16_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::Int32: {
        std::int32_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::UInt32: {
        std::uint32_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::Int64: {
        std::int64_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::UInt64:
        return bits;

    case DataType::Float32: {
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    case DataType::Float64: {
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    }

    throw std::runtime_error("invalid shared memory data type");
}

} // namespace

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

SharedMemoryManager::SharedMemoryManager(
    std::string name,
    std::uint64_t configVersion,
    const std::vector<PointConfig> &points)
    : name_(std::move(name)),
      configVersion_(configVersion),
      points_(points) {

    if (name_.empty() || name_[0] != '/') {
        name_ = "/" + name_;
    }

    for (std::size_t i = 0; i < points_.size(); ++i) {
        index_[points_[i].id] = i;
    }
}

SharedMemoryManager::~SharedMemoryManager() {
    if (mapping_) {
        munmap(mapping_, mappingSize_);
    }

    if (fd_ >= 0) {
        close(fd_);
    }

    if (!name_.empty()) {
        shm_unlink(name_.c_str());
    }
}

void SharedMemoryManager::create() {
    shm_unlink(name_.c_str());

    fd_ = shm_open(
        name_.c_str(),
        O_CREAT | O_EXCL | O_RDWR,
        0600);

    if (fd_ < 0) {
        throw std::runtime_error(
            "shm_open failed: " +
            std::string(std::strerror(errno)));
    }

    mappingSize_ =
        sizeof(SharedHeader) +
        points_.size() * sizeof(PointRecord);

    if (ftruncate(fd_, static_cast<off_t>(mappingSize_)) != 0) {
        throw std::runtime_error("ftruncate failed");
    }

    mapping_ = mmap(
        nullptr,
        mappingSize_,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd_,
        0);

    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        throw std::runtime_error("mmap failed");
    }

    std::memset(mapping_, 0, mappingSize_);

    header_ = static_cast<SharedHeader *>(mapping_);

    records_ = reinterpret_cast<PointRecord *>(
        static_cast<char *>(mapping_) +
        sizeof(SharedHeader));

    std::memcpy(header_->magic, "PYCOMSHM", 8);

    header_->abiVersion = kShmAbiVersion;
    header_->headerSize = sizeof(SharedHeader);
    header_->recordSize = sizeof(PointRecord);
    header_->pointCount =
        static_cast<std::uint32_t>(points_.size());

    header_->configVersion = configVersion_;
    header_->managerPid = getpid();

    heartbeat();

    for (std::size_t i = 0; i < points_.size(); ++i) {
        records_[i].pointId = points_[i].id;

        records_[i].dataType =
            static_cast<std::uint16_t>(
                points_[i].dataType);

        write(
            points_[i].id,
            points_[i].initialValue,
            PointStatus::Good);
    }
}

void SharedMemoryManager::write(
    std::uint32_t pointId,
    const Value &value,
    PointStatus status) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = index_.find(pointId);

    if (it == index_.end()) {
        throw std::runtime_error("unknown point id");
    }

    auto &record = records_[it->second];

    const auto type =
        static_cast<DataType>(record.dataType);

    const auto bits =
        encodeValue(value, type);

    __atomic_add_fetch(
        &record.sequence,
        1,
        __ATOMIC_RELEASE);

    record.valueBits = bits;

    record.status =
        static_cast<std::uint16_t>(status);

    record.timestampNs = nowNs();

    __atomic_add_fetch(
        &record.sequence,
        1,
        __ATOMIC_RELEASE);

    __atomic_add_fetch(
        &header_->generation,
        1,
        __ATOMIC_RELEASE);
}

PointSnapshot SharedMemoryManager::read(
    std::uint32_t pointId) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = index_.find(pointId);

    if (it == index_.end()) {
        throw std::runtime_error("unknown point id");
    }

    const auto &record = records_[it->second];

    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto before =
            __atomic_load_n(
                &record.sequence,
                __ATOMIC_ACQUIRE);

        if (before & 1U) {
            continue;
        }

        PointSnapshot result;

        result.pointId = record.pointId;

        result.dataType =
            static_cast<DataType>(record.dataType);

        result.status =
            static_cast<PointStatus>(record.status);

        result.timestampNs =
            record.timestampNs;

        const auto bits =
            record.valueBits;

        const auto after =
            __atomic_load_n(
                &record.sequence,
                __ATOMIC_ACQUIRE);

        if (before == after && !(after & 1U)) {
            result.sequence = after;

            result.value =
                decodeValue(
                    bits,
                    result.dataType);

            return result;
        }
    }

    throw std::runtime_error(
        "could not obtain consistent point snapshot");
}

void SharedMemoryManager::heartbeat() {
    if (header_) {
        __atomic_store_n(
            &header_->managerHeartbeatNs,
            nowNs(),
            __ATOMIC_RELEASE);
    }
}

std::vector<PointSnapshot>
SharedMemoryManager::snapshot() const {

    std::vector<PointSnapshot> result;

    result.reserve(points_.size());

    for (const auto &p : points_) {
        result.push_back(read(p.id));
    }

    return result;
}

} // namespace demo