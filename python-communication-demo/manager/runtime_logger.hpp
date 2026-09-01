#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace demo {

class RuntimeLogger {
public:
    RuntimeLogger(std::string path, std::size_t maxBytes = 1024 * 1024);
    void log(const std::string &level, const std::string &event,
             const std::string &detail);
private:
    void rotateIfNeeded();
    std::string path_;
    std::size_t maxBytes_;
    std::mutex mutex_;
};

} // namespace demo
