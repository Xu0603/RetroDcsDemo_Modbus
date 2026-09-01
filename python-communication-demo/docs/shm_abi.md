# 共享内存 ABI v1

点数据共享内存使用固定宽度字段，本机进程共享本机字节序。

## Header

- magic: `PYCOMSHM`
- abiVersion: 1
- headerSize / recordSize
- pointCount / configVersion
- generation / managerPid / managerHeartbeatNs

## PointRecord

- pointId: uint32
- dataType/status: uint16
- sequence/timestampNs/valueBits: uint64

`valueBits` 固定 8 字节。BOOL/32 位值使用低位，FLOAT 使用位复制。写入时 sequence 变奇数，完成后变偶数。

## Runner IPC 共享内存

IPC 区使用 magic `PYCOMIPC`、ABI 版本 1、进程 epoch、双方 PID/心跳，以及两个
64 槽环形队列。每槽由 `uint32 length` 和 4092 字节 payload 组成。它与点数据区分离，
Python runner 无权直接访问点记录。
