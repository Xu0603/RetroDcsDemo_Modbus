PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- ============================================================
-- Schema Version
-- ============================================================

CREATE TABLE IF NOT EXISTS schema_version (
version INTEGER NOT NULL
);

-- ============================================================
-- Configuration Version
-- ============================================================
---------------------------------------------------------------


-- status:
-- DRAFT      编辑中的配置
-- PUBLISHED  当前正式运行的配置
-- ARCHIVED   历史配置
------------------

-- 系统只允许存在一个 PUBLISHED 配置。
-- ============================================================

CREATE TABLE IF NOT EXISTS config_version (
id INTEGER PRIMARY KEY,
version_no INTEGER NOT NULL UNIQUE,

status TEXT NOT NULL
    CHECK(status IN ('DRAFT','PUBLISHED','ARCHIVED')),

created_at TEXT NOT NULL
    DEFAULT CURRENT_TIMESTAMP,

content_hash TEXT NOT NULL

);

CREATE UNIQUE INDEX IF NOT EXISTS uq_one_published
ON config_version(status)
WHERE status = 'PUBLISHED';

-- ============================================================
-- System Point
-- ============================================================
---------------------------------------------------------------



-- Python
-- Modbus
-- OPC UA（未来）
-- MQTT（未来）
-----------


-- 新增：
-- INT16
-- UINT16
---------

-- 用于支持 Modbus 单寄存器数据。
-- ============================================================

CREATE TABLE IF NOT EXISTS point (
id INTEGER PRIMARY KEY,

version_id INTEGER NOT NULL
    REFERENCES config_version(id),

system_point_id INTEGER NOT NULL,

name TEXT NOT NULL,

data_type TEXT NOT NULL
    CHECK(data_type IN (
        'BOOL',

        'INT16',
        'UINT16',

        'INT32',
        'UINT32',

        'INT64',
        'UINT64',

        'FLOAT32',
        'FLOAT64'
    )),

direction TEXT NOT NULL
    CHECK(direction IN (
        'INPUT',
        'OUTPUT',
        'BIDIRECTIONAL'
    )),

initial_value TEXT NOT NULL
    DEFAULT '0',

enabled INTEGER NOT NULL
    DEFAULT 1
    CHECK(enabled IN (0,1)),

UNIQUE(version_id, system_point_id),

UNIQUE(version_id, name)

);

-- ============================================================
-- Python Communication Task
-- ============================================================
---------------------------------------------------------------


-- 保持原结构，不修改其功能。
-- ============================================================

CREATE TABLE IF NOT EXISTS python_task (
id INTEGER PRIMARY KEY,

version_id INTEGER NOT NULL
    REFERENCES config_version(id),

name TEXT NOT NULL,

script_path TEXT NOT NULL,

entry_function TEXT NOT NULL
    DEFAULT 'on_timer',

trigger_type TEXT NOT NULL
    CHECK(trigger_type IN (
        'PERIODIC',
        'POINT_CHANGE',
        'MANUAL'
    )),

interval_ms INTEGER NOT NULL
    DEFAULT 1000
    CHECK(interval_ms >= 10),

timeout_ms INTEGER NOT NULL
    DEFAULT 1000
    CHECK(timeout_ms >= 10),

restart_policy TEXT NOT NULL
    DEFAULT 'ON_FAILURE'
    CHECK(restart_policy IN (
        'NEVER',
        'ON_FAILURE',
        'ALWAYS'
    )),

enabled INTEGER NOT NULL
    DEFAULT 1
    CHECK(enabled IN (0,1)),

UNIQUE(version_id, name)

);

-- ============================================================
-- Python Point Binding
-- ============================================================
---------------------------------------------------------------

-- Python Task
--      │
--      │ Binding
--      ▼
--    Point
-----------

-- 保持原结构。
-- ============================================================

CREATE TABLE IF NOT EXISTS python_binding (
id INTEGER PRIMARY KEY,

task_id INTEGER NOT NULL
    REFERENCES python_task(id)
    ON DELETE CASCADE,

point_id INTEGER NOT NULL
    REFERENCES point(id),

alias TEXT NOT NULL,

access_mode TEXT NOT NULL
    CHECK(access_mode IN (
        'READ',
        'WRITE',
        'READ_WRITE'
    )),

deadband REAL NOT NULL
    DEFAULT 0.0
    CHECK(deadband >= 0),

enabled INTEGER NOT NULL
    DEFAULT 1
    CHECK(enabled IN (0,1)),

UNIQUE(task_id, alias),

UNIQUE(task_id, point_id)

);

-- ============================================================
-- Modbus Device
-- ============================================================
---------------------------------------------------------------





-- name:
-- PLC01
--------

-- host:
-- 127.0.0.1
------------

-- port:
-- 502
------

-- slave_id:
-- 1
----

-- 一个 Modbus Device 可以拥有多个 Modbus Binding。
-- ============================================================

CREATE TABLE IF NOT EXISTS modbus_device (
id INTEGER PRIMARY KEY,

version_id INTEGER NOT NULL
    REFERENCES config_version(id)
    ON DELETE CASCADE,

name TEXT NOT NULL,

protocol TEXT NOT NULL
    DEFAULT 'TCP'
    CHECK(protocol IN (
        'TCP'
    )),

host TEXT NOT NULL,

port INTEGER NOT NULL
    DEFAULT 502
    CHECK(port >= 1 AND port <= 65535),

slave_id INTEGER NOT NULL
    DEFAULT 1
    CHECK(slave_id >= 0 AND slave_id <= 255),

timeout_ms INTEGER NOT NULL
    DEFAULT 1000
    CHECK(timeout_ms >= 10),

reconnect_interval_ms INTEGER NOT NULL
    DEFAULT 3000
    CHECK(reconnect_interval_ms >= 100),

enabled INTEGER NOT NULL
    DEFAULT 1
    CHECK(enabled IN (0,1)),

UNIQUE(version_id, name)

);

-- ============================================================
-- Modbus Point Binding
-- ============================================================
---------------------------------------------------------------


-- System Point
--        ↑
--        │
-- Modbus Binding
--        │
--        ↓
-- Modbus Device
----------------


-- Device:
-- PLC01
--------

-- Point:
-- Temperature
--------------

-- function_code:
-- 3
----

-- address:
-- 0
----

-- data_type:
-- FLOAT32
----------

-- byte_order:
-- ABCD
-------

-- period_ms:
-- 1000
-------

-- ============================================================

CREATE TABLE IF NOT EXISTS modbus_binding (
id INTEGER PRIMARY KEY,

device_id INTEGER NOT NULL
    REFERENCES modbus_device(id)
    ON DELETE CASCADE,

point_id INTEGER NOT NULL
    REFERENCES point(id),

-- Modbus Function Code
--
-- 1 = Read Coils
-- 2 = Read Discrete Inputs
-- 3 = Read Holding Registers
-- 4 = Read Input Registers
--
function_code INTEGER NOT NULL
    CHECK(function_code IN (
        1,
        2,
        3,
        4
    )),

-- Modbus 起始地址
address INTEGER NOT NULL
    CHECK(address >= 0 AND address <= 65535),

-- 数据类型
--
-- 与 point.data_type 保持一致。
--
data_type TEXT NOT NULL
    CHECK(data_type IN (
        'BOOL',

        'INT16',
        'UINT16',

        'INT32',
        'UINT32',

        'INT64',
        'UINT64',

        'FLOAT32',
        'FLOAT64'
    )),

-- 字节序
--
-- ABCD : 标准顺序
-- BADC : 字节交换
-- CDAB : Word交换
-- DCBA : 完全反转
--
byte_order TEXT NOT NULL
    DEFAULT 'ABCD'
    CHECK(byte_order IN (
        'ABCD',
        'BADC',
        'CDAB',
        'DCBA'
    )),

-- 数据采集周期（毫秒）
period_ms INTEGER NOT NULL
    DEFAULT 1000
    CHECK(period_ms >= 10),

enabled INTEGER NOT NULL
    DEFAULT 1
    CHECK(enabled IN (0,1)),

UNIQUE(device_id, point_id)

);

-- ============================================================
-- Modbus Query Index
-- ============================================================
---------------------------------------------------------------

-- 提高根据设备读取 Binding 的效率。
-- ============================================================

CREATE INDEX IF NOT EXISTS idx_modbus_binding_device
ON modbus_binding(device_id);

CREATE INDEX IF NOT EXISTS idx_modbus_binding_point
ON modbus_binding(point_id);
