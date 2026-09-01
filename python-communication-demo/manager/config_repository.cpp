#include "manager/config_repository.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace demo {
namespace {

// ============================================================
// SQLite Statement RAII
// ============================================================

class Statement {
public:
Statement(sqlite3 *db, const char *sql)
{
if (sqlite3_prepare_v2(
db,
sql,
-1,
&stmt_,
nullptr
) != SQLITE_OK)
{
throw std::runtime_error(
sqlite3_errmsg(db)
);
}
}


~Statement()
{
    sqlite3_finalize(stmt_);
}

sqlite3_stmt *get() const
{
    return stmt_;
}


private:
sqlite3_stmt *stmt_{};
};

// ============================================================
// SQLite TEXT Column
// ============================================================

std::string textColumn(
sqlite3_stmt *stmt,
int column
)
{
const auto *p =
sqlite3_column_text(stmt, column);


return p
    ? reinterpret_cast<const char *>(p)
    : std::string{};


}

// ============================================================
// Modbus Byte Order Parser
// ============================================================

ModbusByteOrder parseModbusByteOrder(
const std::string &text
)
{
if (text == "ABCD")
return ModbusByteOrder::ABCD;

if (text == "BADC")
    return ModbusByteOrder::BADC;

if (text == "CDAB")
    return ModbusByteOrder::CDAB;

if (text == "DCBA")
    return ModbusByteOrder::DCBA;

throw std::runtime_error(
    "unsupported Modbus byte order: " + text
);


}

} // namespace

// ============================================================
// Constructor
// ============================================================

ConfigRepository::ConfigRepository(
std::string path
)
: path_(std::move(path))
{
}

// ============================================================
// Destructor
// ============================================================

ConfigRepository::~ConfigRepository()
{
if (db_)
sqlite3_close(db_);
}

// ============================================================
// Open Database
// ============================================================

void ConfigRepository::open()
{
if (sqlite3_open_v2(
path_.c_str(),
&db_,
SQLITE_OPEN_READONLY,
nullptr
) != SQLITE_OK)
{
throw std::runtime_error(
"cannot open SQLite database: " + path_
);
}


sqlite3_busy_timeout(
    db_,
    2000
);


// --------------------------------------------------------
// Check schema version
// --------------------------------------------------------

Statement version(
    db_,
    "SELECT version FROM schema_version LIMIT 1"
);

if (sqlite3_step(version.get()) != SQLITE_ROW ||
    sqlite3_column_int(version.get(), 0) != 1)
{
    throw std::runtime_error(
        "unsupported or missing schema version"
    );
}

}

// ============================================================
// Load Published Configuration
// ============================================================

RuntimeConfig ConfigRepository::loadPublished()
{
if (!db_)
{
throw std::runtime_error(
"database not open"
);
}

RuntimeConfig config;


// ========================================================
// 1. Load Published Config Version
// ========================================================

{
    Statement stmt(
        db_,
        "SELECT id, version_no, content_hash "
        "FROM config_version "
        "WHERE status='PUBLISHED'"
    );

    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
    {
        throw std::runtime_error(
            "no published configuration"
        );
    }

    config.versionId =
        static_cast<std::uint32_t>(
            sqlite3_column_int64(
                stmt.get(),
                0
            )
        );

    config.versionNo =
        static_cast<std::uint32_t>(
            sqlite3_column_int64(
                stmt.get(),
                1
            )
        );

    config.contentHash =
        textColumn(
            stmt.get(),
            2
        );


    // 系统理论上只能有一个 PUBLISHED 配置
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
    {
        throw std::runtime_error(
            "more than one published configuration"
        );
    }
}


// ========================================================
// 2. Load System Points
// ========================================================

{
    Statement stmt(
        db_,
        "SELECT "
        "id,"
        "system_point_id,"
        "name,"
        "data_type,"
        "direction,"
        "initial_value,"
        "enabled "
        "FROM point "
        "WHERE version_id=? "
        "ORDER BY id"
    );

    sqlite3_bind_int64(
        stmt.get(),
        1,
        config.versionId
    );


    while (
        sqlite3_step(stmt.get()) == SQLITE_ROW
    )
    {
        PointConfig p;

        p.id =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    0
                )
            );

        p.systemPointId =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    1
                )
            );

        p.name =
            textColumn(
                stmt.get(),
                2
            );

        p.dataType =
            parseDataType(
                textColumn(
                    stmt.get(),
                    3
                )
            );

        p.direction =
            textColumn(
                stmt.get(),
                4
            );

        p.initialValue =
            parseValue(
                p.dataType,
                textColumn(
                    stmt.get(),
                    5
                )
            );

        p.enabled =
            sqlite3_column_int(
                stmt.get(),
                6
            ) != 0;

        config.points.push_back(
            std::move(p)
        );
    }
}


// ========================================================
// 3. Load Python Tasks
// ========================================================

std::unordered_map<
    std::uint32_t,
    std::size_t
> taskIndex;


{
    Statement stmt(
        db_,
        "SELECT "
        "id,"
        "name,"
        "script_path,"
        "entry_function,"
        "trigger_type,"
        "interval_ms,"
        "timeout_ms,"
        "restart_policy,"
        "enabled "
        "FROM python_task "
        "WHERE version_id=? "
        "ORDER BY id"
    );

    sqlite3_bind_int64(
        stmt.get(),
        1,
        config.versionId
    );


    while (
        sqlite3_step(stmt.get()) == SQLITE_ROW
    )
    {
        TaskConfig t;

        t.id =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    0
                )
            );

        t.name =
            textColumn(
                stmt.get(),
                1
            );

        t.scriptPath =
            textColumn(
                stmt.get(),
                2
            );

        t.entryFunction =
            textColumn(
                stmt.get(),
                3
            );

        t.triggerType =
            textColumn(
                stmt.get(),
                4
            );

        t.intervalMs =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    5
                )
            );

        t.timeoutMs =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    6
                )
            );

        t.restartPolicy =
            textColumn(
                stmt.get(),
                7
            );

        t.enabled =
            sqlite3_column_int(
                stmt.get(),
                8
            ) != 0;


        taskIndex[t.id] =
            config.tasks.size();

        config.tasks.push_back(
            std::move(t)
        );
    }
}


// ========================================================
// 4. Load Python Bindings
// ========================================================

{
    Statement stmt(
        db_,
        "SELECT "
        "b.task_id,"
        "b.point_id,"
        "b.alias,"
        "b.access_mode,"
        "b.deadband "
        "FROM python_binding b "
        "JOIN python_task t "
        "ON t.id=b.task_id "
        "WHERE "
        "t.version_id=? "
        "AND b.enabled=1 "
        "ORDER BY b.id"
    );

    sqlite3_bind_int64(
        stmt.get(),
        1,
        config.versionId
    );


    while (
        sqlite3_step(stmt.get()) == SQLITE_ROW
    )
    {
        BindingConfig b;

        const auto taskId =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    0
                )
            );

        b.pointId =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    1
                )
            );

        b.alias =
            textColumn(
                stmt.get(),
                2
            );

        b.accessMode =
            textColumn(
                stmt.get(),
                3
            );

        b.deadband =
            sqlite3_column_double(
                stmt.get(),
                4
            );


        const auto it =
            taskIndex.find(
                taskId
            );

        if (it == taskIndex.end())
        {
            throw std::runtime_error(
                "binding references unknown task"
            );
        }

        config.tasks[
            it->second
        ].bindings.push_back(
            std::move(b)
        );
    }
}


// ========================================================
// 5. Load Modbus Devices
// ========================================================
//
// modbus_device
//
// 一个设备对应：
//
// IP
// Port
// Slave ID
// Timeout
// Reconnect Interval
//
// ========================================================

std::unordered_map<
    std::uint32_t,
    std::size_t
> modbusDeviceIndex;


{
    Statement stmt(
        db_,
        "SELECT "
        "id,"
        "name,"
        "protocol,"
        "host,"
        "port,"
        "slave_id,"
        "timeout_ms,"
        "reconnect_interval_ms,"
        "enabled "
        "FROM modbus_device "
        "WHERE version_id=? "
        "ORDER BY id"
    );

    sqlite3_bind_int64(
        stmt.get(),
        1,
        config.versionId
    );


    while (
        sqlite3_step(stmt.get()) == SQLITE_ROW
    )
    {
        ModbusDeviceConfig device;

        device.id =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    0
                )
            );

        device.name =
            textColumn(
                stmt.get(),
                1
            );

        device.protocol =
            textColumn(
                stmt.get(),
                2
            );

        device.host =
            textColumn(
                stmt.get(),
                3
            );

        device.port =
            static_cast<std::uint16_t>(
                sqlite3_column_int(
                    stmt.get(),
                    4
                )
            );

        device.slaveId =
            static_cast<std::uint8_t>(
                sqlite3_column_int(
                    stmt.get(),
                    5
                )
            );

        device.timeoutMs =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    6
                )
            );

        device.reconnectIntervalMs =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    7
                )
            );

        device.enabled =
            sqlite3_column_int(
                stmt.get(),
                8
            ) != 0;


        modbusDeviceIndex[
            device.id
        ] =
            config.modbusDevices.size();


        config.modbusDevices.push_back(
            std::move(device)
        );
    }
}


// ========================================================
// 6. Load Modbus Bindings
// ========================================================
//
// 一个 Modbus Device 可以有多个 Binding
//
// Device
//    │
//    ├── FC03 Address 0
//    │
//    ├── FC03 Address 2
//    │
//    └── FC04 Address 10
//
// ========================================================

{
    Statement stmt(
        db_,
        "SELECT "
        "b.device_id,"
        "b.point_id,"
        "b.function_code,"
        "b.address,"
        "b.data_type,"
        "b.byte_order,"
        "b.period_ms,"
        "b.enabled "
        "FROM modbus_binding b "
        "JOIN modbus_device d "
        "ON d.id=b.device_id "
        "WHERE "
        "d.version_id=? "
        "ORDER BY b.id"
    );

    sqlite3_bind_int64(
        stmt.get(),
        1,
        config.versionId
    );


    while (
        sqlite3_step(stmt.get()) == SQLITE_ROW
    )
    {
        ModbusBindingConfig binding;

        const auto deviceId =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    0
                )
            );

        binding.pointId =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    1
                )
            );

        binding.functionCode =
            static_cast<std::uint8_t>(
                sqlite3_column_int(
                    stmt.get(),
                    2
                )
            );

        binding.address =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    3
                )
            );

        binding.dataType =
            parseDataType(
                textColumn(
                    stmt.get(),
                    4
                )
            );

        binding.byteOrder =
            parseModbusByteOrder(
                textColumn(
                    stmt.get(),
                    5
                )
            );

        binding.periodMs =
            static_cast<std::uint32_t>(
                sqlite3_column_int64(
                    stmt.get(),
                    6
                )
            );

        binding.enabled =
            sqlite3_column_int(
                stmt.get(),
                7
            ) != 0;


        const auto it =
            modbusDeviceIndex.find(
                deviceId
            );

        if (
            it ==
            modbusDeviceIndex.end()
        )
        {
            throw std::runtime_error(
                "Modbus binding references unknown device"
            );
        }


        config.modbusDevices[
            it->second
        ].bindings.push_back(
            std::move(binding)
        );
    }
}


return config;


}

// ============================================================
// Validate Runtime Configuration
// ============================================================

void ConfigRepository::validate(
const RuntimeConfig &config,
const std::string &scriptsRoot
)
{
// ========================================================
// Validate Points
// ========================================================

if (config.points.empty())
{
    throw std::runtime_error(
        "published configuration has no points"
    );
}


std::set<std::uint32_t> pointIds;
std::set<std::uint32_t> systemIds;
std::set<std::string> names;


std::unordered_map<
    std::uint32_t,
    DataType
> pointTypes;


for (
    const auto &p :
    config.points
)
{
    if (
        !pointIds.insert(
            p.id
        ).second
    )
    {
        throw std::runtime_error(
            "duplicate point id"
        );
    }

    if (
        !systemIds.insert(
            p.systemPointId
        ).second
    )
    {
        throw std::runtime_error(
            "duplicate system point id"
        );
    }

    if (
        !names.insert(
            p.name
        ).second
    )
    {
        throw std::runtime_error(
            "duplicate point name"
        );
    }


    pointTypes[
        p.id
    ] =
        p.dataType;
}


// ========================================================
// Validate Python Tasks
// ========================================================

for (
    const auto &task :
    config.tasks
)
{
    if (!task.enabled)
        continue;


    if (task.bindings.empty())
    {
        throw std::runtime_error(
            "enabled task has no bindings: " +
            task.name
        );
    }


    const auto root =
        std::filesystem::weakly_canonical(
            scriptsRoot
        );

    const auto script =
        std::filesystem::weakly_canonical(
            root /
            task.scriptPath
        );


    const auto scriptString =
        script.string();

    const auto rootString =
        root.string();


    if (
        scriptString.compare(
            0,
            rootString.size(),
            rootString
        ) != 0
    )
    {
        throw std::runtime_error(
            "script path escapes scripts root: " +
            task.scriptPath
        );
    }


    if (
        !std::filesystem::is_regular_file(
            script
        )
    )
    {
        throw std::runtime_error(
            "script file does not exist: " +
            script.string()
        );
    }


    std::set<std::string> aliases;


    for (
        const auto &b :
        task.bindings
    )
    {
        if (
            !pointIds.count(
                b.pointId
            )
        )
        {
            throw std::runtime_error(
                "binding references unknown point"
            );
        }


        if (
            !aliases.insert(
                b.alias
            ).second
        )
        {
            throw std::runtime_error(
                "duplicate alias in task: " +
                b.alias
            );
        }


        if (
            b.alias.find_first_of(
                "|;,\n\r"
            ) != std::string::npos
        )
        {
            throw std::runtime_error(
                "alias contains reserved characters: " +
                b.alias
            );
        }
    }
}


// ========================================================
// Validate Modbus Devices
// ========================================================

std::set<std::uint32_t> modbusDeviceIds;
std::set<std::string> modbusDeviceNames;


for (
    const auto &device :
    config.modbusDevices
)
{
    if (
        !modbusDeviceIds.insert(
            device.id
        ).second
    )
    {
        throw std::runtime_error(
            "duplicate Modbus device id"
        );
    }


    if (
        !modbusDeviceNames.insert(
            device.name
        ).second
    )
    {
        throw std::runtime_error(
            "duplicate Modbus device name: " +
            device.name
        );
    }


    // ----------------------------------------------------
    // Disabled device does not need runtime validation
    // ----------------------------------------------------

    if (!device.enabled)
        continue;


    // ----------------------------------------------------
    // Protocol
    // ----------------------------------------------------

    if (
        device.protocol != "TCP"
    )
    {
        throw std::runtime_error(
            "unsupported Modbus protocol: " +
            device.protocol
        );
    }


    // ----------------------------------------------------
    // Host
    // ----------------------------------------------------

    if (
        device.host.empty()
    )
    {
        throw std::runtime_error(
            "Modbus device host is empty: " +
            device.name
        );
    }


    // ----------------------------------------------------
    // Port
    // ----------------------------------------------------

    if (
        device.port == 0
    )
    {
        throw std::runtime_error(
            "invalid Modbus port: " +
            device.name
        );
    }


    // ----------------------------------------------------
    // Timeout
    // ----------------------------------------------------

    if (
        device.timeoutMs < 10
    )
    {
        throw std::runtime_error(
            "invalid Modbus timeout: " +
            device.name
        );
    }


    // ----------------------------------------------------
    // Reconnect Interval
    // ----------------------------------------------------

    if (
        device.reconnectIntervalMs < 100
    )
    {
        throw std::runtime_error(
            "invalid Modbus reconnect interval: " +
            device.name
        );
    }


    // ----------------------------------------------------
    // Enabled Device must have bindings
    // ----------------------------------------------------

    bool hasEnabledBinding = false;

    std::set<std::uint32_t> devicePointIds;


    for (
        const auto &binding :
        device.bindings
    )
    {
        if (!binding.enabled)
            continue;


        hasEnabledBinding = true;


        // ------------------------------------------------
        // Point exists
        // ------------------------------------------------

        const auto pointIt =
            pointTypes.find(
                binding.pointId
            );


        if (
            pointIt ==
            pointTypes.end()
        )
        {
            throw std::runtime_error(
                "Modbus binding references unknown point"
            );
        }


        // ------------------------------------------------
        // Same Point cannot be bound twice
        // ------------------------------------------------

        if (
            !devicePointIds.insert(
                binding.pointId
            ).second
        )
        {
            throw std::runtime_error(
                "duplicate Modbus point binding in device: " +
                device.name
            );
        }


        // ------------------------------------------------
        // Point DataType must match Modbus DataType
        // ------------------------------------------------

        if (
            pointIt->second !=
            binding.dataType
        )
        {
            throw std::runtime_error(
                "Modbus data type mismatch for point id: " +
                std::to_string(
                    binding.pointId
                )
            );
        }


        // ------------------------------------------------
        // Function Code
        // ------------------------------------------------

        switch (
            binding.functionCode
        )
        {
        case 1:
        case 2:
        case 3:
        case 4:
            break;

        default:
            throw std::runtime_error(
                "unsupported Modbus function code"
            );
        }


        // ------------------------------------------------
        // Address
        // ------------------------------------------------

        if (
            binding.address > 65535
        )
        {
            throw std::runtime_error(
                "invalid Modbus address"
            );
        }


        // ------------------------------------------------
        // Period
        // ------------------------------------------------

        if (
            binding.periodMs < 10
        )
        {
            throw std::runtime_error(
                "invalid Modbus period"
            );
        }


        // ------------------------------------------------
        // Function Code and DataType compatibility
        // ------------------------------------------------
        //
        // FC01 / FC02
        //     only BOOL
        //
        // FC03 / FC04
        //     register types
        // ------------------------------------------------

        if (
            binding.functionCode == 1 ||
            binding.functionCode == 2
        )
        {
            if (
                binding.dataType !=
                DataType::Bool
            )
            {
                throw std::runtime_error(
                    "Modbus coil/input function requires BOOL"
                );
            }
        }


        if (
            binding.functionCode == 3 ||
            binding.functionCode == 4
        )
        {
            if (
                binding.dataType ==
                DataType::Bool
            )
            {
                throw std::runtime_error(
                    "Modbus register function cannot use BOOL"
                );
            }
        }
    }


    if (!hasEnabledBinding)
    {
        throw std::runtime_error(
            "enabled Modbus device has no bindings: " +
            device.name
        );
    }
}


}

} // namespace demo
