# Python Runner 调试输出协议

每个 Runner 在 `127.0.0.1` 启动一个调试 TCP Server，端口为 `20000 + taskId % 40000`。
它只用于本机调试，不接受脚本执行或数据写入命令。

Runner 用兼容文本流接管 `sys.stdout` 和 `sys.stderr`，原内容仍写到终端，同时保存最近
2000 行。客户端发送：

```text
GET <cursor>\n
```

Server 返回 UTF-8 JSON Lines。普通行包含 `sequence`、`timestampNs`、`stream` 和
`text`；最后一行是 `{"type":"END","cursor":n}`。上位保存每个任务的 cursor，因而
只显示新增输出。Runner 重启后序号从零开始，上位会自动重新同步。
