# Python SDK v1

脚本入口可选：`on_start(ctx)`、任务配置的主回调（示例 `on_timer(ctx)`）、`on_stop(ctx)`。

`ctx.read(alias)` 返回 `PointValue(value, data_type, status, timestamp_ns, sequence)`。

`ctx.write(alias, value)` 只接受精确的 `bool`、`int`、`float`，manager 按 SQLite 类型执行严格转换。

其他接口：`read_many`、`write_many`、`subscribe`、`log`、`task_id`、`config_version`。

脚本不应：

- 打开配置 SQLite；
- 直接映射点数据共享内存或自行操作 IPC 共享内存；
- 保存底层 IPC 句柄或在后台线程并发调用 ctx；
- 捕获并忽略所有异常；
- 依赖未列入离线锁定文件的包。
