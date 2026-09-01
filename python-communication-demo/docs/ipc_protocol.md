# IPC 协议 v1

每个 runner 拥有一块 IPC 共享内存，其中包含请求队列（Python → C++）和响应队列
（C++ → Python）。每队列 64 个固定槽位，每槽最多 4092 字节。槽内 payload 每行一个
`key=value`，键和值使用百分号编码。

每条队列配有三个 POSIX 命名信号量：`slots` 统计空槽，`items` 统计可读消息，
`mutex` 保护读写索引和槽位。生产者先等待空槽和互斥锁，写完再通知 `items`；消费者按
相反顺序处理。所有等待都有超时，进程 epoch 用于拒绝旧 runner。

基础字段：`type`、`request_id`、`task_id`、`protocol_version`、`process_epoch`。

消息：

- `HELLO` / `HELLO_ACK`
- `TASK_CONFIG`
- `READ_REQUEST` / `READ_RESPONSE`
- `WRITE_REQUEST` / `WRITE_RESULT`
- `SUBSCRIBE_REQUEST` / `SUBSCRIBE_RESPONSE` / `VALUE_UPDATE`
- `HEARTBEAT` / `HEARTBEAT_ACK`
- `SCRIPT_LOG` / `SCRIPT_EXCEPTION` / `EXECUTION_STAT`
- `TASK_STOP` / `SHUTDOWN`

错误码：`OK`、`BAD_REQUEST`、`VERSION_MISMATCH`、`TASK_NOT_FOUND`、
`ALIAS_NOT_FOUND`、`PERMISSION_DENIED`、`TYPE_MISMATCH`、`OUT_OF_RANGE`、
`STALE_PROCESS`、`TIMEOUT`、`INTERNAL_ERROR`。
