# 错误码

| 错误码 | 含义 | Python侧处理 |
|---|---|---|
| OK | 成功 | 继续 |
| BAD_REQUEST | 消息缺字段或类型不支持 | 记录并修复 SDK/脚本 |
| VERSION_MISMATCH | IPC 版本不一致 | 拒绝启动任务 |
| TASK_NOT_FOUND | 任务不在已发布配置中 | 拒绝启动任务 |
| ALIAS_NOT_FOUND | alias 未绑定 | 抛出脚本异常 |
| PERMISSION_DENIED | 点方向或绑定权限不允许 | 抛出脚本异常并审计 |
| TYPE_MISMATCH | Python 值无法安全转换 | 拒绝写入 |
| OUT_OF_RANGE | 超出工程或整数范围 | 拒绝写入 |
| STALE_PROCESS | 旧 epoch 的迟到进程 | 断开连接 |
| TIMEOUT | 请求或回调超时 | 熔断/重启任务 |
| INTERNAL_ERROR | manager 内部错误 | 记录诊断并按策略重试 |
