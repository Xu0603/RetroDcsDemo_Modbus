# Retro DCS Qt5 界面 Demo

这是依据《第三方通信管理软件总体设计方案》制作的界面风格验证原型：

- Qt 5 Widgets，macOS 可直接编译运行；
- MACS/传统 DCS 工程师站式工作台；
- Win98 风格的灰色 3D 控件、深蓝标题、紧凑表格和 16×16 像素图标；
- 包含工程树、通道总览、点表、报文监视、属性检查器、问题/日志面板；
- 工具栏可启动/停止本工程的 `system-managerd`，并通过 TCP 打开在线点数据；
- 系统总览、在线数据、报文监视和运行日志均连接真实 Demo 运行链路。

## macOS 构建

### 使用 Qt Creator / qmake

在 Qt Creator 中直接打开 `RetroDcsDemo.pro`，选择 Qt 5 Desktop Kit 后运行即可。

也可以在终端构建：

```bash
mkdir -p build-qmake
cd build-qmake
/opt/homebrew/opt/qt@5/bin/qmake ../RetroDcsDemo.pro
make -j4
./retro_dcs_demo
```

## 联动操作

1. 在工程树展开“采集设备 → Python 通信”，右键“脚本任务”可新建脚本和绑定点；
2. 右键具体任务可查看、编辑或删除；双击任务直接进入编辑；
3. 双击“点目录”可统一查看、筛选全工程点及其任务和脚本别名；
4. “写入 SQLite”使用事务同时保存任务、点和绑定关系；
5. 点击工具栏“启动检测”，上位会先 PING，再按需启动 `system-managerd`；
6. 点击“在线数据”查看 TCP 返回的点快照；
7. “停止检测”只停止由当前上位启动的 manager，不终止外部服务。

在线数据表中右键点可执行“强制值”，manager 会完成配置版本和数据类型校验后写入共享
内存并记录日志。报文监视显示每个 Runner 的 `print()`、`ctx.log()` 和 stderr 增量内容；
表格中使用短摘要，单击后可在下方查看完整多行文本。暂停期间 Runner 输出保留在环形缓存，
点击继续后从上次位置补取。

点由脚本任务的“绑定点配置”统一维护，因此工程树中不再保留重复的“点配置”节点。
“点目录”只负责全局汇总查看。删除任务会删除其绑定及未被其他任务使用的孤立点，但不会
删除磁盘上的 Python 文件。

绑定点配置支持 CSV 批量导入和导出，固定列为：
`system_point_id,name,data_type,direction,alias,access_mode,initial_value`。导入只更新编辑表格，
确认无误后仍需点击“写入 SQLite”才会保存。manager 运行日志只记录启动、退出、断链、超时、
脚本异常和强制值等运维事件；普通脚本输出统一在“报文监视”中查看。

## 工程目录

通过“新建工程”创建的工程相互隔离：

```text
工程名称/
├── project.json       # 工程名称和相对路径
├── config.sqlite      # 任务、点及绑定配置
├── scripts/           # Python 脚本
└── run/               # 启动检测后生成的运行日志
```

程序会记住最近打开的 `project.json`。切换工程时会停止由当前上位启动的 manager；TCP
PING 同时校验配置指纹，防止把其他工程或旧配置的在线值显示到当前工程中。

“打开工程”选择包含 `project.json` 的工程目录；没有最近工程或关闭工程后，工程区保持空白。
“工程 → 修改工程名称”只更新 `project.json` 中的显示名称，不重命名目录，也不改变 SQLite
和脚本路径。

### 使用 CMake

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)"
cmake --build build -j
./build/retro_dcs_demo
```

如果 CMake 找不到 Qt5，也可先执行：

```bash
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
```

## 风格说明

界面刻意不调用 macOS 原生 Aqua 外观，而通过 Qt StyleSheet 与自绘像素图标保持跨平台一致。中文字体优先使用 `Songti SC`/`SimSun`（宋体），不存在时回退到系统衬线字体。当前原型以 1280×800 为基准，最小窗口尺寸为 1024×680。
