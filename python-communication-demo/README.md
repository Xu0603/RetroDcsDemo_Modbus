# Python 通信闭环 Demo

本工程实现总体设计中的 P0–P5，不包含真实 StructManage 适配：

- C++17 `system-managerd` 只读解析 SQLite 已发布配置；
- manager 创建点数据共享内存，并为每个 runner 创建独立 IPC 共享内存；
- C++/Python 通过双向环形队列传递消息，命名信号量负责互斥、空槽和消息通知；
- manager 在 `127.0.0.1:19090` 提供带固定帧头的 TCP 查询接口，供上位检测和读取在线快照；
- Python 回调带超时，异常不影响 manager；
- runner 具备心跳、退出检测和指数退避重启；
- 运行日志按大小轮转，退出时可生成点快照。

## 构建

```bash
cd python-communication-demo
make          # 编译 system-managerd 和组件测试
make test     # 建库并执行组件、协议、崩溃重启测试
make run      # 运行 6 秒端到端示例
```

如需指定编译器或 Python：`make CXX=clang++ PYTHON=python3`。CMakeLists.txt
仍保留作为可选构建方式，但日常开发以顶层 Makefile 为准。

## 运行端到端示例

```bash
python3 tools/run_demo.py
```

故障恢复验证（会杀死第一个 runner，并检查自动重启）：

```bash
python3 tests/test_restart.py
```

运行 6 秒后，`run/snapshot.json` 中的 `calculated_value` 应为：

```text
25.0 × 0.6 + 1.2 × 0.4 = 15.48
```

运行事件写入 `run/runtime.log`。共享内存在 manager 正常退出时自动解除链接。

## 关键边界

Python 业务脚本不读取 SQLite、不知道任何共享内存偏移。Runner SDK 负责映射 IPC
共享内存，业务脚本只使用：

```python
temperature = ctx.read("temperature").value
ctx.write("calculated_value", temperature * 0.6)
```

manager 根据 SQLite 中的 alias、类型和方向完成权限检查、类型转换及共享内存写入。

## 上位 TCP 查询

TCP 帧头固定 16 字节：magic `PYSM`、协议版本、消息类型、请求 ID、JSON payload
长度，所有整数使用网络字节序。消息类型 `1` 为 PING，`2` 为完整点快照；返回类型为
请求类型加 `0x8000`。快照携带配置版本、点 ID、类型、值、质量、时间戳和序号。

## Linux 部署

`deploy/systemd/` 提供无交互式 sudo 的 systemd 示例。安装阶段创建 `thirdcomm` 用户和目录权限；运行期服务使用专用账户及 `NoNewPrivileges`，不依赖 sudo。
