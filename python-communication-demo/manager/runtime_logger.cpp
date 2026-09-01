#include "manager/runtime_logger.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace demo {

RuntimeLogger::RuntimeLogger(std::string path, std::size_t maxBytes)
    : path_(std::move(path)), maxBytes_(maxBytes) {
    const auto parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
}

void RuntimeLogger::rotateIfNeeded() {
    std::error_code ec;
    if (std::filesystem::file_size(path_, ec) < maxBytes_ || ec) return;
    std::filesystem::remove(path_ + ".2", ec);
    std::filesystem::rename(path_ + ".1", path_ + ".2", ec);
    std::filesystem::rename(path_, path_ + ".1", ec);
}

void RuntimeLogger::log(const std::string &level, const std::string &event,
                        const std::string &detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    rotateIfNeeded();
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream stamp;
    stamp << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
    std::ofstream out(path_, std::ios::app);
    out << stamp.str() << " level=" << level << " event=" << event
        << " detail=" << detail << '\n';
}

} // namespace demo
