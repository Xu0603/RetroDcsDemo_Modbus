# P0 架构契约

## 进程边界

- `system-managerd` 是 SQLite 配置、运行点目录和 POSIX 共享内存的唯一所有者及写入者。
- `python-runner` 是独立进程，不打开 SQLite、不映射点数据共享内存；它只映射由
  manager 为该任务创建的 IPC 共享内存。
- 一个 runner 对应一个任务；崩溃、超时和依赖异常不影响 manager。

## 数据路径

```text
SQLite -> ConfigRepository -> PointCatalog -> SharedMemory
Python script -> Runner Context -> 请求环形队列 -> CommandGuard -> Point SharedMemory
Point SharedMemory -> manager typed read -> 响应环形队列 -> Python value/status/timestamp
```

## 第一版限制

- 只实现 Fake/自建 POSIX 共享内存，不实现真实 StructManage 适配。
- IPC 使用固定槽位的双向环形队列，payload 为 UTF-8 字段协议，接口版本固定为 1。
- 每条队列使用 `slots/items/mutex` 三个 POSIX 命名信号量；满队列和锁等待均有超时。
- 字符串只用于 IPC；共享内存不含指针和动态对象。
- manager 单写，序列号使用奇偶序列保护一致性读取。
