#pragma once

#include "common/types.hpp"

#include <string>

struct sqlite3;

namespace demo {

class ConfigRepository {
public:
explicit ConfigRepository(std::string path);
~ConfigRepository();


ConfigRepository(const ConfigRepository &) = delete;
ConfigRepository &operator=(const ConfigRepository &) = delete;

// 打开 SQLite 配置数据库
void open();

// 加载当前 PUBLISHED 配置
//
// 包含：
// - System Points
// - Python Tasks
// - Python Bindings
// - Modbus Devices
// - Modbus Bindings
RuntimeConfig loadPublished();

// 校验完整运行配置
static void validate(
    const RuntimeConfig &config,
    const std::string &scriptsRoot
);

private:
std::string path_;
sqlite3 *db_{};
};

} // namespace demo
