# 上位在线查询协议 v1

`system-managerd` 默认只监听 `127.0.0.1:19090`。配置修改不通过该接口；强制点值必须
由上位明确确认并携带当前配置指纹。

## 帧格式

固定 16 字节帧头，整数均为网络字节序：

- magic：4 字节 `PYSM`
- protocolVersion：uint16，当前为 1
- messageType：uint16
- requestId：uint32
- payloadLength：uint32，最大 1 MiB
- payload：UTF-8 JSON

请求类型 `1` 为 PING，`2` 为 SNAPSHOT，`3` 为 FORCE_WRITE；成功响应类型等于请求类型加 `0x8000`。
快照包含 `configVersion`、manager PID，以及每个点的 ID、系统点 ID、名称、数据类型、
方向、值、质量、时间戳和序号。

FORCE_WRITE 的 JSON 包含 `pointId`、字符串形式的 `value` 和 `configHash`。manager 校验
工程配置指纹并按点数据类型转换，然后写入点共享内存；失败返回 `POINT_NOT_FOUND`、
`TYPE_MISMATCH` 或 `CONFIG_MISMATCH`。所有强制操作写入运行日志。

## 安全边界

- 默认仅回环地址可访问；跨机部署必须另加认证和 TLS，不能直接绑定 `0.0.0.0`。
- 上位必须校验 magic、协议版本、请求 ID、payload 长度和配置版本。
- Python runner 继续使用共享内存和命名信号量；manager TCP 服务于上位检测、在线展示和强制值。
