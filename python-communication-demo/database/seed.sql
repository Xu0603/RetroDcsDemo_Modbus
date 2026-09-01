-- ============================================================
-- Clear old data
-- ============================================================

DELETE FROM modbus_binding;
DELETE FROM modbus_device;

DELETE FROM python_binding;
DELETE FROM python_task;

DELETE FROM point;

DELETE FROM config_version;

DELETE FROM schema_version;


-- ============================================================
-- Schema Version
-- ============================================================

INSERT INTO schema_version(version)
VALUES (1);


-- ============================================================
-- Configuration Version
-- ============================================================

INSERT INTO config_version(
    id,
    version_no,
    status,
    content_hash
)
VALUES (
    1,
    1,
    'PUBLISHED',
    'demo-v1'
);


-- ============================================================
-- System Points
-- ============================================================

-- 原 Python Demo 点

INSERT INTO point(
    id,
    version_id,
    system_point_id,
    name,
    data_type,
    direction,
    initial_value
)
VALUES
(
    1,
    1,
    1001,
    'temperature',
    'FLOAT64',
    'INPUT',
    '25.0'
),
(
    2,
    1,
    1002,
    'pressure',
    'FLOAT64',
    'INPUT',
    '1.2'
),
(
    3,
    1,
    1003,
    'calculated_value',
    'FLOAT64',
    'OUTPUT',
    '0.0'
);


-- ============================================================
-- Modbus Test Points
-- ============================================================

INSERT INTO point(
    id,
    version_id,
    system_point_id,
    name,
    data_type,
    direction,
    initial_value
)
VALUES

-- UINT16
(
    10,
    1,
    2001,
    'modbus_uint16',
    'UINT16',
    'INPUT',
    '0'
),

-- INT16
(
    11,
    1,
    2002,
    'modbus_int16',
    'INT16',
    'INPUT',
    '0'
),

-- UINT32
(
    12,
    1,
    2003,
    'modbus_uint32',
    'UINT32',
    'INPUT',
    '0'
),

-- INT32
(
    13,
    1,
    2004,
    'modbus_int32',
    'INT32',
    'INPUT',
    '0'
),

-- FLOAT32
(
    14,
    1,
    2005,
    'modbus_float32',
    'FLOAT32',
    'INPUT',
    '0'
),

-- FLOAT64
(
    15,
    1,
    2006,
    'modbus_float64',
    'FLOAT64',
    'INPUT',
    '0'
);


-- ============================================================
-- Python Task
-- ============================================================

INSERT INTO python_task(
    id,
    version_id,
    name,
    script_path,
    entry_function,
    trigger_type,
    interval_ms,
    timeout_ms,
    restart_policy,
    enabled
)
VALUES (
    1,
    1,
    'weighted_calculation',
    'weighted_calculation.py',
    'on_timer',
    'PERIODIC',
    1000,
    800,
    'ON_FAILURE',
    0
);


-- ============================================================
-- Python Binding
-- ============================================================

INSERT INTO python_binding(
    id,
    task_id,
    point_id,
    alias,
    access_mode
)
VALUES
(
    1,
    1,
    1,
    'temperature',
    'READ'
),
(
    2,
    1,
    2,
    'pressure',
    'READ'
),
(
    3,
    1,
    3,
    'calculated_value',
    'WRITE'
);


-- ============================================================
-- Modbus Device
-- ============================================================

INSERT INTO modbus_device(
    id,
    version_id,
    name,
    protocol,
    host,
    port,
    slave_id,
    timeout_ms,
    reconnect_interval_ms,
    enabled
)
VALUES
(
    1,
    1,
    'ModbusSimulator',
    'TCP',
    '127.0.0.1',
    502,
    1,
    1000,
    3000,
    1
);


-- ============================================================
-- Modbus Bindings
-- ============================================================

-- UINT16
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    1,
    1,
    10,
    3,
    0,
    'UINT16',
    'ABCD',
    1000,
    1
);


-- INT16
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    2,
    1,
    11,
    3,
    1,
    'INT16',
    'ABCD',
    1000,
    1
);


-- UINT32
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    3,
    1,
    12,
    3,
    2,
    'UINT32',
    'ABCD',
    1000,
    1
);


-- INT32
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    4,
    1,
    13,
    3,
    4,
    'INT32',
    'ABCD',
    1000,
    1
);


-- FLOAT32
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    5,
    1,
    14,
    3,
    6,
    'FLOAT32',
    'ABCD',
    1000,
    1
);


-- FLOAT64
INSERT INTO modbus_binding(
    id,
    device_id,
    point_id,
    function_code,
    address,
    data_type,
    byte_order,
    period_ms,
    enabled
)
VALUES
(
    6,
    1,
    15,
    3,
    8,
    'FLOAT64',
    'ABCD',
    1000,
    1
);
