#include "mainwindow.h"
#include "retro_style.h"
#include "manager_client.h"
#include "csv_utils.h"
#include "project_metadata.h"
#include "modbus_shared_memory_reader.h"

#include <QtWidgets>
#include <QtNetwork>
#include <QtSql>

#define RETRO_STRINGIFY_INNER(value) #value
#define RETRO_STRINGIFY(value) RETRO_STRINGIFY_INNER(value)

namespace {

class MainWindow final : public QMainWindow
{
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("第三方通信管理系统"));
        setWindowIcon(pixelIcon(QColor("#000080"), 3));
        resize(1320, 820);
        setMinimumSize(1024, 680);
        createMenus();
        createToolbar();
        createWorkspace();
        createStatusBar();
        openBundledDemo();
        startDemoTimer();
    }

    ~MainWindow() override
    {
        if (managerProcess_ && managerProcess_->state() != QProcess::NotRunning) {
            managerProcess_->terminate();
            if (!managerProcess_->waitForFinished(1500)) managerProcess_->kill();
        }
    }

    void openIntegrationPreview()
    {
        startManager();
        showOnlineData();
    }

    bool openProjectPath(const QString &path)
    {
        const QFileInfo info(path);
        return loadProjectFile(info.isDir() ? QDir(path).filePath(QStringLiteral("project.json")) : path);
    }

    void openScriptEditorPreview() { showScriptEditor(1, false); }

private:
    QTabWidget *tabs_ = nullptr;
    QTableWidget *overview_ = nullptr;
    QTableWidget *frames_ = nullptr;
    QPlainTextEdit *frameDetail_ = nullptr;
    QPlainTextEdit *runtimeLog_ = nullptr;
    QTableWidget *problems_ = nullptr;
    QTabWidget *logTabs_ = nullptr;
    QTableWidget *onlineTable_ = nullptr;
    QTimer *modbusTimer_ = nullptr;
    QString currentCommunicationType_ =
    QStringLiteral("python");

    bool modbusMode_ = false;
    ModbusSharedMemoryReader modbusSharedMemoryReader_;
    QTimer *modbusRefreshTimer_ = nullptr;
    QLabel *clock_ = nullptr;
    QLabel *rxRate_ = nullptr;
    QLabel *serviceStatus_ = nullptr;
    QLabel *projectVersionLabel_ = nullptr;
    QLabel *taskSummary_ = nullptr;
    QLabel *pointSummary_ = nullptr;
    QLabel *currentProjectLabel_ = nullptr;
    QTreeWidget *projectTree_ = nullptr;
    QTreeWidgetItem *projectRootItem_ = nullptr;
    QTreeWidgetItem *pythonTasksItem_ = nullptr;
    QTreeWidgetItem *pointDirectoryItem_ = nullptr;
    QAction *startManagerAction_ = nullptr;
    QAction *stopManagerAction_ = nullptr;
    QProcess *managerProcess_ = nullptr;
    int onlineTabIndex_ = -1;
    quint32 requestId_ = 0;
    QString currentProjectDirectory_;
    QString currentProjectName_;
    QString currentDatabase_;
    QString currentScriptsRoot_;
    
    QString currentProjectMetadata_;
    QMap<qlonglong, QString> debugTasks_;
    QHash<qlonglong, qlonglong> debugCursors_;
    bool framePaused_ = false;
    qint64 runtimeLogOffset_ = 0;
    QString runtimeLogPending_;

    void createMenus()
    {
        const QStringList menus = {
            QStringLiteral("工程(&P)"), QStringLiteral("编辑(&E)"),
            QStringLiteral("组态(&C)"), QStringLiteral("运行(&R)"),
            QStringLiteral("监视(&M)"), QStringLiteral("工具(&T)"),
            QStringLiteral("窗口(&W)"), QStringLiteral("帮助(&H)")
        };
        for (const auto &name : menus) {
            auto *menu = menuBar()->addMenu(name);
            if (name.startsWith(QStringLiteral("工程"))) {
                menu->addAction(pixelIcon(QColor("#c8a000"), 0), QStringLiteral("新建工程..."), this, [this] { createProject(); });
                menu->addAction(pixelIcon(QColor("#c8a000"), 0), QStringLiteral("打开工程..."), this, [this] { openProject(); });
                menu->addAction(QStringLiteral("修改工程名称..."), this, [this] { renameProject(); });
                menu->addAction(pixelIcon(QColor("#000080"), 1), QStringLiteral("保存全部"));
                menu->addAction(QStringLiteral("关闭工程"), this, [this] { closeProject(); });
                menu->addSeparator();
                menu->addAction(QStringLiteral("退出"), this, &QWidget::close);
            } else if (name.startsWith(QStringLiteral("运行"))) {
                menu->addAction(pixelIcon(QColor("#008000"), 4), QStringLiteral("启动所选通道"));
                menu->addAction(pixelIcon(QColor("#800000"), 2), QStringLiteral("停止所选通道"));
            } else {
                menu->addAction(QStringLiteral("演示命令"));
            }
        }
    }

    void createToolbar()
    {
        auto *tb = addToolBar(QStringLiteral("主工具栏"));
        tb->setMovable(false);
        tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        auto *newProjectAction = tb->addAction(pixelIcon(QColor("#c8a000"), 0), QStringLiteral("新建"));
        auto *openProjectAction = tb->addAction(pixelIcon(QColor("#c8a000"), 0), QStringLiteral("打开"));
        connect(newProjectAction, &QAction::triggered, this, [this] { createProject(); });
        connect(openProjectAction, &QAction::triggered, this, [this] { openProject(); });
        const QList<QPair<QString, QIcon>> configActions = {
            {QStringLiteral("保存"), pixelIcon(QColor("#000080"), 1)},
            {QStringLiteral("校验"), pixelIcon(QColor("#008080"), 4)}
        };
        for (const auto &a : configActions) {
            auto *act = tb->addAction(a.second, a.first);
            act->setToolTip(a.first);
        }
        tb->addSeparator();
        startManagerAction_ = tb->addAction(pixelIcon(QColor("#008000"), 4), QStringLiteral("启动检测"));
        stopManagerAction_ = tb->addAction(pixelIcon(QColor("#800000"), 2), QStringLiteral("停止检测"));
        auto *overviewAction = tb->addAction(pixelIcon(QColor("#000080"), 3), QStringLiteral("系统总览"));
        auto *onlineAction = tb->addAction(pixelIcon(QColor("#008080"), 3), QStringLiteral("在线数据"));
        auto *framesAction = tb->addAction(pixelIcon(QColor("#000080"), 3), QStringLiteral("报文监视"));
        startManagerAction_->setToolTip(QStringLiteral("检测并启动 system-managerd"));
        stopManagerAction_->setToolTip(QStringLiteral("停止由本上位启动的 system-managerd"));
        connect(startManagerAction_, &QAction::triggered, this, [this] { startManager(); });
        connect(stopManagerAction_, &QAction::triggered, this, [this] { stopManager(); });
        connect(overviewAction, &QAction::triggered, this, [this] { showOverview(); });
        connect(onlineAction, &QAction::triggered, this, [this] { showOnlineData(); });
        connect(framesAction, &QAction::triggered, this, [this] { showFrameMonitor(); });
        tb->addSeparator();
        currentProjectLabel_ = new QLabel;
        currentProjectLabel_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        currentProjectLabel_->setVisible(false);
        tb->addWidget(currentProjectLabel_);
    }

    void createWorkspace()
    {
        tabs_ = new QTabWidget;
        tabs_->setTabsClosable(true);
        tabs_->setMovable(true);
        tabs_->addTab(createOverviewPage(), pixelIcon(QColor("#000080"), 3), QStringLiteral("系统总览"));
        tabs_->addTab(createFramePage(), pixelIcon(QColor("#800080"), 3), QStringLiteral("报文监视"));
        connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
            QWidget *page = tabs_->widget(index);
            const QString title = tabs_->tabText(index);
            tabs_->removeTab(index);
            if (title == QStringLiteral("系统总览")) {
                overview_ = nullptr; taskSummary_ = nullptr; pointSummary_ = nullptr;
            } else if (title == QStringLiteral("报文监视")) {
                frames_ = nullptr; frameDetail_ = nullptr; rxRate_ = nullptr;
            } else if (title == QStringLiteral("在线数据")) {
                onlineTable_ = nullptr; onlineTabIndex_ = -1;
            }
            page->deleteLater();
        });
        setCentralWidget(tabs_);

        auto *projectDock = new QDockWidget(QStringLiteral("工程管理器"), this);
        projectDock->setObjectName(QStringLiteral("projectDock"));
        projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        projectDock->setWidget(createProjectTree());
        addDockWidget(Qt::LeftDockWidgetArea, projectDock);

        auto *propertyDock = new QDockWidget(QStringLiteral("属性 / 检查器"), this);
        propertyDock->setObjectName(QStringLiteral("propertyDock"));
        propertyDock->setWidget(createPropertyPanel());
        addDockWidget(Qt::RightDockWidgetArea, propertyDock);

        auto *logDock = new QDockWidget(QStringLiteral("问题与运行日志"), this);
        logDock->setObjectName(QStringLiteral("logDock"));
        logDock->setWidget(createLogPanel());
        addDockWidget(Qt::BottomDockWidgetArea, logDock);

        resizeDocks({projectDock, propertyDock}, {235, 275}, Qt::Horizontal);
        resizeDocks({logDock}, {175}, Qt::Vertical);
    }

    QWidget *createProjectTree()
    {
        auto *box = new QWidget;
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(4, 4, 4, 4);
        auto *filter = new QLineEdit;
        filter->setPlaceholderText(QStringLiteral("查找工程对象"));
        layout->addWidget(filter);

        auto *tree = new QTreeWidget;
        projectTree_ = tree;
        tree->setHeaderLabel(QStringLiteral("对象名称"));
        tree->setAnimated(false);
        auto add = [&](QTreeWidgetItem *parent, const QString &name, QColor c, int kind) {
            auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
            item->setText(0, name);
            item->setIcon(0, pixelIcon(c, kind));
            return item;
        };
        auto *root = add(nullptr, QString(), QColor("#c8a000"), 0);
        projectRootItem_ = root;
        root->setData(0, Qt::UserRole, QStringLiteral("project_root"));
        root->setHidden(true);
        auto *channels = add(root, QStringLiteral("采集设备"), QColor("#000080"), 3);
        auto *python = add(channels, QStringLiteral("Python 通信"), QColor("#008080"), 3);
        auto *pythonTasks = add(python, QStringLiteral("脚本任务"), QColor("#008080"), 1);
        pythonTasksItem_ = pythonTasks;
        pythonTasks->setData(0, Qt::UserRole, QStringLiteral("python_tasks"));
        pointDirectoryItem_ = add(root, QStringLiteral("点目录"), QColor("#c8a000"), 0);
        pointDirectoryItem_->setData(0, Qt::UserRole, QStringLiteral("point_directory"));
        root->setExpanded(true);
        channels->setExpanded(true);
        python->setExpanded(true);
        tree->setCurrentItem(pythonTasks);
        layout->addWidget(tree);

        connect(filter, &QLineEdit::textChanged, tree, [tree](const QString &text) {
            for (auto *item : tree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive))
                item->setHidden(!text.isEmpty() && !item->text(0).contains(text, Qt::CaseInsensitive));
        });
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree, &QTreeWidget::customContextMenuRequested, this, [this, tree](const QPoint &position) {
            auto *item = tree->itemAt(position);
            if (!item) return;
            const QString kind = item->data(0, Qt::UserRole).toString();
            QMenu menu(tree);
            if (kind == QStringLiteral("project_root")) {
                auto *rename = menu.addAction(QStringLiteral("修改工程名称..."));
                if (menu.exec(tree->viewport()->mapToGlobal(position)) == rename) renameProject();
            } else if (kind == QStringLiteral("python_tasks")) {
                auto *create = menu.addAction(pixelIcon(QColor("#008080"), 1), QStringLiteral("新建脚本..."));
                if (menu.exec(tree->viewport()->mapToGlobal(position)) == create) showScriptEditor();
            } else if (kind == QStringLiteral("python_task")) {
                auto *view = menu.addAction(QStringLiteral("查看配置"));
                auto *edit = menu.addAction(QStringLiteral("编辑配置..."));
                menu.addSeparator();
                auto *remove = menu.addAction(pixelIcon(QColor("#800000"), 2), QStringLiteral("删除任务..."));
                QAction *selected = menu.exec(tree->viewport()->mapToGlobal(position));
                const qlonglong taskId = item->data(0, Qt::UserRole + 1).toLongLong();
                if (selected == view) showScriptEditor(taskId, true);
                else if (selected == edit) showScriptEditor(taskId, false);
                else if (selected == remove) deleteScriptTask(taskId);
            } else if (kind == QStringLiteral("point_directory")) {
                auto *view = menu.addAction(QStringLiteral("查看点目录"));
                if (menu.exec(tree->viewport()->mapToGlobal(position)) == view) showPointDirectory();
            }
        });
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
            const QString kind = item->data(0, Qt::UserRole).toString();
            if (kind == QStringLiteral("python_tasks")) showScriptEditor();
            else if (kind == QStringLiteral("python_task"))
                showScriptEditor(item->data(0, Qt::UserRole + 1).toLongLong(), false);
            else if (kind == QStringLiteral("point_directory")) showPointDirectory();
        });
        return box;
    }

    QWidget *createOverviewPage()
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        auto *title = new QLabel(QStringLiteral(" 通信运行总览"));
        title->setStyleSheet(QStringLiteral("background:#000080;color:white;font-weight:bold;padding:5px;"));
        layout->addWidget(title);

        overview_ = new QTableWidget(0, 7);
        overview_->setHorizontalHeaderLabels({
            QStringLiteral("状态"), QStringLiteral("通道"), QStringLiteral("协议/角色"),
            QStringLiteral("对端"), QStringLiteral("收/发帧"), QStringLiteral("成功率"), QStringLiteral("响应")
        });
        overview_->setAlternatingRowColors(true);
        overview_->setSelectionBehavior(QAbstractItemView::SelectRows);
        overview_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        overview_->horizontalHeader()->setStretchLastSection(true);
        overview_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        layout->addWidget(overview_);

        auto *summary = new QHBoxLayout;
        for (const auto &pair : QList<QPair<QString, QString>>{
                 {QStringLiteral("运行任务"), QStringLiteral("0 / 0")},
                 {QStringLiteral("工程点数"), QStringLiteral("0 点")}}) {
            auto *g = new QGroupBox(pair.first);
            auto *l = new QVBoxLayout(g);
            auto *v = new QLabel(pair.second);
            v->setAlignment(Qt::AlignCenter);
            v->setStyleSheet(QStringLiteral("font-size:18px;font-weight:bold;color:#000080;"));
            l->addWidget(v);
            if (pair.first == QStringLiteral("运行任务")) taskSummary_ = v;
            if (pair.first == QStringLiteral("工程点数")) pointSummary_ = v;
            summary->addWidget(g);
        }
        layout->addLayout(summary);
        return page;
    }

    QWidget *createPointPage()
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        auto *controls = new QHBoxLayout;
        auto *search = new QLineEdit;
        search->setPlaceholderText(QStringLiteral("点名/地址/描述"));
        controls->addWidget(new QLabel(QStringLiteral("扫描组:")));
        auto *group = new QComboBox;
        group->addItems({QStringLiteral("快速组 (100 ms)"), QStringLiteral("普通组 (1 s)"), QStringLiteral("慢速组 (5 s)")});
        controls->addWidget(group);
        controls->addSpacing(8);
        controls->addWidget(new QLabel(QStringLiteral("筛选:")));
        controls->addWidget(search, 1);
        controls->addWidget(new QPushButton(QStringLiteral("导入...")));
        controls->addWidget(new QPushButton(QStringLiteral("分包预览")));
        layout->addLayout(controls);

        auto *table = new QTableWidget(12, 9);
        table->setHorizontalHeaderLabels({
            QStringLiteral("启用"), QStringLiteral("点名"), QStringLiteral("地址"),
            QStringLiteral("类型"), QStringLiteral("读写"), QStringLiteral("工程值"),
            QStringLiteral("质量"), QStringLiteral("周期"), QStringLiteral("描述")
        });
        for (int r = 0; r < table->rowCount(); ++r) {
            auto *enabled = new QTableWidgetItem;
            enabled->setCheckState(Qt::Checked);
            table->setItem(r, 0, enabled);
            table->setItem(r, 1, new QTableWidgetItem(QStringLiteral("BLR_%1").arg(r % 2 ? QStringLiteral("PRESS") : QStringLiteral("TEMP")) + QString::number(r + 1)));
            table->setItem(r, 2, new QTableWidgetItem(QStringLiteral("400%1%2").arg(10 + r / 2).arg(r % 2 ? QStringLiteral(".1") : QString())));
            table->setItem(r, 3, new QTableWidgetItem(r % 2 ? QStringLiteral("DM/Bit") : QStringLiteral("AM/Float32")));
            table->setItem(r, 4, new QTableWidgetItem(r == 7 ? QStringLiteral("写") : QStringLiteral("读")));
            table->setItem(r, 5, new QTableWidgetItem(r % 2 ? QStringLiteral("1") : QString::number(80.0 + r * 2.35, 'f', 2)));
            table->setItem(r, 6, new QTableWidgetItem(r == 4 ? QStringLiteral("Bad_Comm") : QStringLiteral("Good")));
            table->setItem(r, 7, new QTableWidgetItem(QStringLiteral("100 ms")));
            table->setItem(r, 8, new QTableWidgetItem(QStringLiteral("锅炉工艺演示测点 %1").arg(r + 1)));
            if (r == 4) table->item(r, 6)->setForeground(QColor("#c00000"));
        }
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
        layout->addWidget(table);
        auto *hint = new QLabel(QStringLiteral("  校验结果: 11 个有效点，1 个通信质量异常  |  自动分为 2 个读取块，预计周期占用 43%"));
        hint->setStyleSheet(QStringLiteral("background:#ffffc0;border:1px solid #808000;padding:4px;"));
        layout->addWidget(hint);
        return page;
    }

    QWidget *createFramePage()
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        auto *bar = new QHBoxLayout;
        auto *pause = new QPushButton(QStringLiteral("Ⅱ 暂停"));
        auto *clear = new QPushButton(QStringLiteral("清空"));
        bar->addWidget(pause);
        bar->addWidget(clear);
        auto *source = new QLabel(QStringLiteral("监视来源：Python Runner 标准输出 / 错误输出"));
        source->setStyleSheet(QStringLiteral("color:#000080;"));
        bar->addWidget(source);
        bar->addStretch();
        rxRate_ = sunkenLabel(QStringLiteral("实时采集"), 90);
        bar->addWidget(rxRate_);
        layout->addLayout(bar);

        auto *split = new QSplitter(Qt::Vertical);
        frames_ = new QTableWidget(0, 6);
        frames_->setHorizontalHeaderLabels({
            QStringLiteral("时间"), QStringLiteral("来源"), QStringLiteral("脚本任务"),
            QStringLiteral("摘要"), QStringLiteral("长度"), QStringLiteral("结果")
        });
        frames_->setAlternatingRowColors(true);
        frames_->setSelectionBehavior(QAbstractItemView::SelectRows);
        frames_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        frames_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        split->addWidget(frames_);

        auto *detailPanel = new QWidget;
        auto *detailLayout = new QVBoxLayout(detailPanel);
        detailLayout->setContentsMargins(0, 0, 0, 0);
        detailLayout->addWidget(new QLabel(QStringLiteral("完整内容（单击上方摘要查看）：")));
        frameDetail_ = new QPlainTextEdit;
        frameDetail_->setReadOnly(true);
        frameDetail_->setFont(QFont(QStringLiteral("Monaco"), 11));
        frameDetail_->setPlaceholderText(QStringLiteral("选择一条 Python 输出后在这里显示完整内容。"));
        detailLayout->addWidget(frameDetail_);
        split->addWidget(detailPanel);
        split->setSizes({300, 180});
        layout->addWidget(split);
        connect(clear, &QPushButton::clicked, frames_, [this] {
            frames_->setRowCount(0);
            if (frameDetail_) frameDetail_->clear();
        });
        connect(pause, &QPushButton::clicked, pause, [this, pause] {
            framePaused_ = !framePaused_;
            pause->setText(framePaused_ ? QStringLiteral("▶ 继续") : QStringLiteral("Ⅱ 暂停"));
            if (rxRate_) rxRate_->setText(framePaused_ ? QStringLiteral("已暂停") : QStringLiteral("实时采集"));
        });
        connect(frames_, &QTableWidget::itemSelectionChanged, frames_, [this] {
            if (!frameDetail_) return;
            const int row = frames_->currentRow();
            QTableWidgetItem *summary = row >= 0 ? frames_->item(row, 3) : nullptr;
            frameDetail_->setPlainText(summary ? summary->data(Qt::UserRole).toString() : QString());
        });
        return page;
    }

    QWidget *createPropertyPanel()
    {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto *panel = new QWidget;
        auto *layout = new QVBoxLayout(panel);
        auto addForm = [&](const QString &title, const QList<QPair<QString, QString>> &items) {
            auto *group = new QGroupBox(title);
            auto *form = new QFormLayout(group);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            for (const auto &item : items) {
                auto *edit = new QLineEdit(item.second);
                form->addRow(item.first, edit);
            }
            layout->addWidget(group);
        };
        addForm(QStringLiteral("基本"), {
            {QStringLiteral("通道名称"), QStringLiteral("锅炉房 PLC")},
            {QStringLiteral("协议"), QStringLiteral("Modbus TCP")},
            {QStringLiteral("角色"), QStringLiteral("主站")}
        });
        addForm(QStringLiteral("端点"), {
            {QStringLiteral("IP 地址"), QStringLiteral("10.20.1.31")},
            {QStringLiteral("端口"), QStringLiteral("502")},
            {QStringLiteral("Unit ID"), QStringLiteral("1")}
        });
        addForm(QStringLiteral("时序"), {
            {QStringLiteral("响应超时"), QStringLiteral("1000 ms")},
            {QStringLiteral("重试次数"), QStringLiteral("2")},
            {QStringLiteral("重连间隔"), QStringLiteral("3000 ms")}
        });
        auto *test = new QPushButton(QStringLiteral("测试连接"));
        layout->addWidget(test);
        layout->addStretch();
        scroll->setWidget(panel);
        return scroll;
    }

    QWidget *createLogPanel()
    {
        logTabs_ = new QTabWidget;
        problems_ = new QTableWidget(0, 4);
        problems_->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("级别"), QStringLiteral("事件"), QStringLiteral("说明")});
        problems_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        problems_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        problems_->setSelectionBehavior(QAbstractItemView::SelectRows);
        runtimeLog_ = new QPlainTextEdit;
        runtimeLog_->setReadOnly(true);
        runtimeLog_->setFont(QFont(QStringLiteral("Monaco"), 10));
        runtimeLog_->setPlaceholderText(QStringLiteral("启动检测后，这里显示 system-managerd 的真实运行日志。"));
        logTabs_->addTab(problems_, pixelIcon(QColor("#c8a000"), 2), QStringLiteral("问题 (0)"));
        logTabs_->addTab(runtimeLog_, pixelIcon(QColor("#000080"), 3), QStringLiteral("运行日志"));
        return logTabs_;
    }

    void createStatusBar()
    {
        statusBar()->setSizeGripEnabled(true);
        serviceStatus_ = sunkenLabel(QStringLiteral("○ 检测服务未连接"), 205);
        serviceStatus_->setStyleSheet(QStringLiteral("color:#800000;"));
        statusBar()->addWidget(serviceStatus_);
        projectVersionLabel_ = sunkenLabel(QStringLiteral("工程点数: --"), 220);
        statusBar()->addWidget(projectVersionLabel_, 1);
        statusBar()->addPermanentWidget(sunkenLabel(QStringLiteral("用户: engineer"), 115));
        statusBar()->addPermanentWidget(sunkenLabel(QStringLiteral("本地模式"), 80));
        clock_ = sunkenLabel(QString(), 150);
        statusBar()->addPermanentWidget(clock_);
    }

void startDemoTimer()
{
    auto *timer = new QTimer(this);

    connect(
        timer,
        &QTimer::timeout,
        this,
        [this]
        {
            clock_->setText(
                QDateTime::currentDateTime().toString(
                    QStringLiteral("yyyy-MM-dd  HH:mm:ss")
                )
            );

            // =================================================
            // Modbus 工程
            // =================================================

            if (currentCommunicationType_ == QStringLiteral("modbus"))
            {
                pollModbusSharedMemory();
            }
            else if (onlineTable_)
            {
                // =============================================
                // 原来的 Python / system-manager 数据
                // =============================================

                pollManager();
            }

            if (overview_ || frames_)
                pollPythonDebug();

            // pollRuntimeLog();  // 如果这个函数不存在，请注释掉或实现它
        }
    );

    timer->start(1000);

    clock_->setText(
        QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd  HH:mm:ss")
        )
    );
}


    QString workspaceRoot() const
    {
        QStringList candidates;
#ifdef RETRO_SOURCE_DIR
        candidates << QString::fromUtf8(RETRO_STRINGIFY(RETRO_SOURCE_DIR));
#endif
        candidates << QStringList{
            QDir::currentPath(),
            QCoreApplication::applicationDirPath(),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."))
        };
        for (const auto &candidate : candidates) {
            QDir dir(candidate);
            if (QFileInfo::exists(dir.filePath(QStringLiteral("python-communication-demo"))))
                return dir.absolutePath();
        }
        return QDir::currentPath();
    }

    void openBundledDemo()
    {
        const QString recent = QSettings().value(QStringLiteral("recentProject")).toString();
        if (!recent.isEmpty() && QFileInfo::exists(recent) && loadProjectFile(recent)) return;
        closeProject();
    }

    void setCurrentProject(const QString &name, const QString &directory,
                           const QString &database, const QString &scriptsRoot)
    {
        if (managerProcess_ && managerProcess_->state() != QProcess::NotRunning) stopManager();
        currentProjectName_ = name;
        currentProjectDirectory_ = QDir(directory).absolutePath();
        currentDatabase_ = QFileInfo(database).absoluteFilePath();
        currentScriptsRoot_ = QDir(scriptsRoot).absolutePath();
        debugCursors_.clear();
        runtimeLogOffset_ = 0;
        runtimeLogPending_.clear();
        if (runtimeLog_) runtimeLog_->clear();
        if (problems_) problems_->setRowCount(0);
        if (logTabs_) logTabs_->setTabText(0, QStringLiteral("问题 (0)"));
        setWindowTitle(QStringLiteral("第三方通信管理系统 - %1").arg(name));
        currentProjectLabel_->setText(QStringLiteral(" 当前工程: %1  ").arg(name));
        currentProjectLabel_->setVisible(true);
        if (projectRootItem_) projectRootItem_->setHidden(false);
        rebuildProjectTree();
        statusBar()->showMessage(QStringLiteral("工程已打开：%1").arg(currentProjectDirectory_), 2500);
    }

    void closeProject()
    {
        if (managerProcess_ && managerProcess_->state() != QProcess::NotRunning) stopManager();
        currentProjectName_.clear();
        currentProjectDirectory_.clear();
        currentDatabase_.clear();
        currentScriptsRoot_.clear();
        currentProjectMetadata_.clear();
        debugTasks_.clear();
        debugCursors_.clear();
        runtimeLogOffset_ = 0;
        runtimeLogPending_.clear();
        QSettings().remove(QStringLiteral("recentProject"));
        setWindowTitle(QStringLiteral("第三方通信管理系统"));
        if (currentProjectLabel_) { currentProjectLabel_->clear(); currentProjectLabel_->setVisible(false); }
        if (projectRootItem_) { projectRootItem_->setText(0, QString()); projectRootItem_->setHidden(true); }
        if (pythonTasksItem_)
            while (pythonTasksItem_->childCount()) delete pythonTasksItem_->takeChild(0);
        if (pointDirectoryItem_) pointDirectoryItem_->setText(0, QStringLiteral("点目录 (0)"));
        if (overview_) overview_->setRowCount(0);
        if (frames_) frames_->setRowCount(0);
        if (frameDetail_) frameDetail_->clear();
        if (onlineTable_) onlineTable_->setRowCount(0);
        if (runtimeLog_) runtimeLog_->clear();
        if (problems_) problems_->setRowCount(0);
        if (logTabs_) logTabs_->setTabText(0, QStringLiteral("问题 (0)"));
        if (taskSummary_) taskSummary_->setText(QStringLiteral("0 / 0"));
        if (pointSummary_) pointSummary_->setText(QStringLiteral("0 点"));
        if (projectVersionLabel_) projectVersionLabel_->setText(QStringLiteral("工程点数: --"));
        if (serviceStatus_) {
            serviceStatus_->setText(QStringLiteral("○ 检测服务未连接"));
            serviceStatus_->setStyleSheet(QStringLiteral("color:#800000;"));
        }
        statusBar()->showMessage(QStringLiteral("工程已关闭"), 2500);
    }

    void rebuildProjectTree()
    {
        if (!projectRootItem_ || !pythonTasksItem_) return;
        projectRootItem_->setText(0, currentProjectName_);
        while (pythonTasksItem_->childCount()) delete pythonTasksItem_->takeChild(0);
        int pointCount = 0;
        QList<QVariantList> taskRows;
        debugTasks_.clear();
        const QString connectionName = QStringLiteral("tree_%1").arg(reinterpret_cast<quintptr>(this));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(currentDatabase_);
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral("SELECT id,name,enabled FROM python_task ORDER BY id"))) {
                    while (query.next()) {
                        taskRows.append({query.value(0), query.value(1), query.value(2)});
                        if (query.value(2).toBool()) debugTasks_.insert(query.value(0).toLongLong(), query.value(1).toString());
                        auto *item = new QTreeWidgetItem(pythonTasksItem_);
                        item->setText(0, query.value(1).toString() +
                            (query.value(2).toBool() ? QStringLiteral(" [启用]") : QStringLiteral(" [停用]")));
                        item->setIcon(0, pixelIcon(QColor("#008080"), 1));
                        item->setData(0, Qt::UserRole, QStringLiteral("python_task"));
                        item->setData(0, Qt::UserRole + 1, query.value(0));
                    }
                }
                if (query.exec(QStringLiteral("SELECT COUNT(*) FROM point")) && query.next())
                    pointCount = query.value(0).toInt();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        if (pointDirectoryItem_)
            pointDirectoryItem_->setText(0, QStringLiteral("点目录 (%1)").arg(pointCount));
        projectRootItem_->setExpanded(true);
        pythonTasksItem_->setExpanded(true);
        if (projectVersionLabel_)
            projectVersionLabel_->setText(QStringLiteral("工程点数: %1").arg(pointCount));
        if (overview_) {
            overview_->setRowCount(taskRows.size());
            for (int row = 0; row < taskRows.size(); ++row) {
                const QStringList values = {
                    taskRows[row][2].toBool() ? QStringLiteral("○ 已配置") : QStringLiteral("○ 已停用"),
                    taskRows[row][1].toString(), QStringLiteral("Python / Runner"), QStringLiteral("本机共享内存"),
                    QStringLiteral("--"), QStringLiteral("--"), QStringLiteral("待启动")
                };
                for (int column = 0; column < values.size(); ++column)
                    overview_->setItem(row, column, new QTableWidgetItem(values[column]));
                overview_->item(row, 0)->setData(Qt::UserRole, taskRows[row][0]);
                overview_->item(row, 0)->setForeground(taskRows[row][2].toBool() ? QColor("#000080") : QColor("#808080"));
            }
        }
        if (taskSummary_) taskSummary_->setText(QStringLiteral("0 / %1").arg(taskRows.size()));
        if (pointSummary_) pointSummary_->setText(QStringLiteral("%1 点").arg(pointCount));
    }

    bool loadProjectFile(const QString &metadataPath)
    {
        ProjectMetadata metadata;
        if (!ProjectMetadataFile::load(metadataPath, &metadata)) return false;
        const QDir directory = QFileInfo(metadataPath).absoluteDir();
        const QString database = directory.filePath(metadata.database);
        const QString scripts = directory.filePath(metadata.scripts);
        if (!QFileInfo::exists(database) || !QFileInfo(scripts).isDir()) return false;
        setCurrentProject(metadata.name, directory.absolutePath(), database, scripts);
        currentProjectMetadata_ = QFileInfo(metadataPath).absoluteFilePath();
        QSettings().setValue(QStringLiteral("recentProject"), metadataPath);
        QSettings().setValue(QStringLiteral("lastProjectDirectory"), directory.absolutePath());
        return true;
    }

    void openProject()
    {
        QString initial = currentProjectDirectory_;
        if (initial.isEmpty()) initial = QSettings().value(QStringLiteral("lastProjectDirectory"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
        const QString directory = QFileDialog::getExistingDirectory(this,
            QStringLiteral("选择通信工程目录（目录内应包含 project.json）"), initial);
        if (directory.isEmpty()) return;
        const QString metadata = QDir(directory).filePath(QStringLiteral("project.json"));
        if (!loadProjectFile(metadata))
            QMessageBox::critical(this, QStringLiteral("打开失败"),
                QStringLiteral("所选目录不是有效通信工程。\n\n应包含：\nproject.json\nconfig.sqlite\nscripts/"));
    }

    void renameProject()
    {
        if (currentProjectDirectory_.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("没有工程"), QStringLiteral("请先新建或打开工程。"));
            return;
        }
        bool accepted = false;
        const QString newName = QInputDialog::getText(this, QStringLiteral("修改工程名称"),
            QStringLiteral("工程名称："), QLineEdit::Normal, currentProjectName_, &accepted).trimmed();
        if (!accepted || newName == currentProjectName_) return;
        if (newName.isEmpty() || newName.contains(QRegularExpression(QStringLiteral("[\\r\\n]")))) {
            QMessageBox::warning(this, QStringLiteral("名称无效"), QStringLiteral("工程名称不能为空或包含换行。"));
            return;
        }
        const QString metadataPath = currentProjectMetadata_.isEmpty()
            ? QDir(currentProjectDirectory_).filePath(QStringLiteral("project.json")) : currentProjectMetadata_;
        const ProjectMetadata metadata{newName,
            QDir(currentProjectDirectory_).relativeFilePath(currentDatabase_),
            QDir(currentProjectDirectory_).relativeFilePath(currentScriptsRoot_)};
        QString saveError;
        if (!ProjectMetadataFile::save(metadataPath, metadata, &saveError)) {
            QMessageBox::critical(this, QStringLiteral("修改失败"), saveError);
            return;
        }
        currentProjectName_ = newName;
        currentProjectMetadata_ = metadataPath;
        QSettings().setValue(QStringLiteral("recentProject"), metadataPath);
        setWindowTitle(QStringLiteral("第三方通信管理系统 - %1").arg(newName));
        currentProjectLabel_->setText(QStringLiteral(" 当前工程: %1  ").arg(newName));
        if (projectRootItem_) projectRootItem_->setText(0, newName);
        statusBar()->showMessage(QStringLiteral("工程名称已修改"), 2500);
    }

        void createProject()
{
    QDialog dialog(this);

    dialog.setWindowTitle(QStringLiteral("新建通信工程"));
    dialog.resize(420, 220);

    auto *layout = new QVBoxLayout(&dialog);

    auto *typeLabel =
        new QLabel(QStringLiteral("请选择通信类型："));

    layout->addWidget(typeLabel);


    auto *typeCombo = new QComboBox;

    typeCombo->addItem(
        QStringLiteral("Python 通信工程"),
        QStringLiteral("python")
    );

    typeCombo->addItem(
        QStringLiteral("Modbus 通信工程"),
        QStringLiteral("modbus")
    );

    layout->addWidget(typeCombo);


    auto *nameLabel =
        new QLabel(QStringLiteral("工程名称："));

    layout->addWidget(nameLabel);


    auto *name =
        new QLineEdit(QStringLiteral("新建通信工程"));

    layout->addWidget(name);


    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Ok |
            QDialogButtonBox::Cancel
        );

    layout->addWidget(buttons);


    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept
    );

    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject
    );


    if (dialog.exec() != QDialog::Accepted)
        return;


    const QString projectType =
        typeCombo->currentData().toString();


    const QString projectName =
        name->text().trimmed();


    if (projectName.isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral("工程名称不能为空"),
            QStringLiteral("请输入工程名称。")
        );

        return;
    }


    // =====================================================
    // Modbus 工程
    // =====================================================

    if (projectType == QStringLiteral("modbus"))
{
    createModbusProject(projectName);
    return;
}


// =====================================================
// 其他工程类型暂不处理
// =====================================================

QMessageBox::information(
    this,
    QStringLiteral("提示"),
    QStringLiteral("当前版本暂时只启用 Modbus 工程创建功能。")
);
}

void createModbusProject(const QString &projectName)
{
    // =====================================================
    // Modbus 工程不需要 project.json
    //
    // 因为 ModbusCollector 已经负责：
    //
    // Modbus TCP
    //      ↓
    // 数据采集
    //      ↓
    // POSIX Shared Memory
    // /modbus_data
    //
    // Qt 上位机只负责读取共享内存
    // =====================================================


    currentProjectDirectory_.clear();

    currentDatabase_.clear();

    currentScriptsRoot_.clear();


    setWindowTitle(
        QStringLiteral("Retro DCS - %1 [Modbus]")
            .arg(projectName)
    );


    // =====================================================
    // 保存当前工程类型
    // =====================================================

    currentCommunicationType_ =
        QStringLiteral("modbus");


    // =====================================================
    // 更新状态栏
    // =====================================================

    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 通信工程已创建。数据源：共享内存 /modbus_data"
        ),
        5000
    );


    // =====================================================
    // 通信运行总览
    // =====================================================

    if (overview_)
    {
        overview_->setRowCount(0);

        const int row = overview_->rowCount();

        overview_->insertRow(row);


        const QStringList values = {

            QStringLiteral("○ 离线"),

            projectName,

            QStringLiteral("Modbus TCP"),

            QStringLiteral("共享内存 /modbus_data"),

            QStringLiteral("本机"),

            QStringLiteral("实时采集"),

            QStringLiteral("等待启动")

        };


        for (int column = 0;
             column < values.size();
             ++column)
        {
            overview_->setItem(
                row,
                column,
                new QTableWidgetItem(
                    values[column]
                )
            );
        }
    }


    // =====================================================
    // 清空在线数据
    // =====================================================

    if (onlineTable_)
    {
        onlineTable_->setRowCount(0);
    }


    // =====================================================
    // 修改提示
    // =====================================================

    if (runtimeLog_)
    {
        runtimeLog_->appendPlainText(
            QStringLiteral(
                "[INFO] Modbus 工程创建成功，等待读取共享内存 /modbus_data"
            )
        );
    }
}

    QJsonObject queryManager(quint16 messageType, QString *error = nullptr,
                             const QJsonObject &requestObject = QJsonObject())
    {
        return ManagerClient().request(messageType, requestId_, error, requestObject);
    }

    QString currentConfigHash() const
    {
        if (currentDatabase_.isEmpty()) return {};
        const QString connectionName = QStringLiteral("hash_%1").arg(QCoreApplication::applicationPid());
        QString hash;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(currentDatabase_);
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral("SELECT content_hash FROM config_version WHERE status='PUBLISHED'")) && query.next())
                    hash = query.value(0).toString();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        return hash;
    }

void startModbusDetection()
{
    // =====================================================
    // 尝试读取 ModbusCollector 写入的共享内存
    // =====================================================

    QVector<ModbusSharedVariable> variables;
    QString error;

    if (!modbusSharedMemoryReader_.read(variables, &error))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("未检测到 Modbus 数据"),
            QStringLiteral(
                "无法读取共享内存 /modbus_data。\n\n"
                "错误信息：%1\n\n"
                "请确认：\n"
                "1. ModbusCollector 已经启动\n"
                "2. ModbusCollector 正在采集数据\n"
                "3. ModbusCollector 已经写入共享内存"
            ).arg(error)
        );

        return;
    }


    if (variables.isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral("未检测到 Modbus 数据"),
            QStringLiteral(
                "共享内存 /modbus_data 中暂时没有变量数据。\n\n"
                "请确认 ModbusCollector 已经成功采集数据。"
            )
        );

        return;
    }


    // =====================================================
    // 更新工程状态
    // =====================================================

    if (overview_
        && overview_->rowCount() > 0)
    {
        auto *state =
            overview_->item(0, 0);

        if (state)
        {
            state->setText(
                QStringLiteral("● 运行")
            );

            state->setForeground(
                QColor("#008000")
            );
        }


        if (overview_->item(0, 3))
        {
            overview_->item(0, 3)->setText(
                QStringLiteral(
                    "共享内存 /modbus_data"
                )
            );
        }


        if (overview_->item(0, 6))
        {
            overview_->item(0, 6)->setText(
                QStringLiteral("运行正常")
            );
        }
    }


    // =====================================================
    // 更新一次在线数据
    // =====================================================

    pollModbusSharedMemory();


    // =====================================================
    // 启动定时刷新
    // =====================================================

    if (!modbusTimer_)
    {
        modbusTimer_ =
            new QTimer(this);


        connect(
            modbusTimer_,
            &QTimer::timeout,
            this,
            [this]()
            {
                pollModbusSharedMemory();
            }
        );
    }


    modbusTimer_->start(500);


    serviceStatus_->setText(
        QStringLiteral(
            "● Modbus 数据采集中"
        )
    );


    serviceStatus_->setStyleSheet(
        QStringLiteral(
            "color:#008000;font-weight:bold;"
        )
    );


    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 检测已启动，正在读取共享内存 /modbus_data"
        ),
        3000
    );
}

    void startManager()
{
    // =====================================================
    // Modbus 工程
    // =====================================================

    if (currentCommunicationType_
        == QStringLiteral("modbus"))
    {
        startModbusDetection();

        return;
    }


    // =====================================================
    // 原来的 Python system-managerd 逻辑
    // =====================================================
        // ====================================================
        // Modbus 模式
        // ====================================================
        if (modbusMode_)
        {
            if (!modbusRefreshTimer_)
            {
                modbusRefreshTimer_ = new QTimer(this);

                connect(
                    modbusRefreshTimer_,
                    &QTimer::timeout,
                    this,
                    [this]()
                    {
                        pollModbusSharedMemory();
                    }
                );
            }

            modbusRefreshTimer_->start(1000);

            pollModbusSharedMemory();

            statusBar()->showMessage(
                QStringLiteral("Modbus检测已启动，正在读取共享内存 /modbus_data")
            );

            QMessageBox::information(
                this,
                QStringLiteral("启动检测"),
                QStringLiteral(
                    "Modbus检测已启动。\n\n"
                    "Qt上位机将直接读取 ModbusCollector 写入的共享内存：\n"
                    "/modbus_data"
                )
            );

            return;
        }
        QString error;
        const QJsonObject existing = queryManager(1, &error);
        if (!existing.isEmpty() && existing.value(QStringLiteral("configHash")).toString() != currentConfigHash()) {
            QMessageBox::critical(this, QStringLiteral("检测配置不一致"),
                QStringLiteral("端口 19090 上运行的是其他工程或旧配置。请先停止现有检测服务，再启动当前工程。"));
            return;
        }
        if (!existing.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("system-managerd 已经运行"), 3000);
            pollManager();
            return;
        }
        if (currentDatabase_.isEmpty() || currentScriptsRoot_.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("没有工程"), QStringLiteral("请先新建或打开一个工程。"));
            return;
        }
        bool runnable = false;
        const QString checkConnection = QStringLiteral("run_check_%1").arg(QCoreApplication::applicationPid());
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), checkConnection);
            db.setDatabaseName(currentDatabase_);
            if (db.open()) {
                QSqlQuery query(db);
                if (query.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM python_task WHERE enabled=1), (SELECT COUNT(*) FROM point WHERE enabled=1)")) && query.next())
                    runnable = query.value(0).toInt() > 0 && query.value(1).toInt() > 0;
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(checkConnection);
        if (!runnable) {
            QMessageBox::information(this, QStringLiteral("工程尚未配置"),
                QStringLiteral("请先在“采集设备 → Python 通信 → 脚本任务”中新建脚本并配置点。"));
            return;
        }
        const QDir root(workspaceRoot());
        const QString demo = root.filePath(QStringLiteral("python-communication-demo"));
        const QString executable = QDir(demo).filePath(QStringLiteral("build/system-managerd"));
        const QString database = currentDatabase_;
        if (!QFileInfo(executable).isExecutable() || !QFileInfo::exists(database)) {
            QMessageBox::critical(this, QStringLiteral("无法启动检测"),
                QStringLiteral("缺少 system-managerd 或 SQLite 配置。\n请先执行：\ncd %1 && make demo-db && make").arg(demo));
            return;
        }
        if (managerProcess_) managerProcess_->deleteLater();
        managerProcess_ = new QProcess(this);
        managerProcess_->setWorkingDirectory(demo);
        managerProcess_->setProcessChannelMode(QProcess::MergedChannels);
        connect(managerProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code) {
            statusBar()->showMessage(QStringLiteral("system-managerd 已退出，代码 %1").arg(code), 5000);
            pollManager();
        });
        const QString shmName = QStringLiteral("/pycom_ui_%1").arg(QCoreApplication::applicationPid());
        QDir(currentProjectDirectory_).mkpath(QStringLiteral("run"));
        managerProcess_->start(executable, {
            QStringLiteral("--database"), database,
            QStringLiteral("--scripts-root"), currentScriptsRoot_,
            QStringLiteral("--shm-name"), shmName,
            QStringLiteral("--log"), QDir(currentProjectDirectory_).filePath(QStringLiteral("run/runtime.log")),
            QStringLiteral("--python"), QStringLiteral("python3"),
            QStringLiteral("--runner"), QDir(demo).filePath(QStringLiteral("runner/runner.py")),
            QStringLiteral("--tcp-host"), QStringLiteral("127.0.0.1"),
            QStringLiteral("--tcp-port"), QStringLiteral("19090")
        });
        if (!managerProcess_->waitForStarted(1500)) {
            QMessageBox::critical(this, QStringLiteral("启动失败"), managerProcess_->errorString());
            return;
        }
        statusBar()->showMessage(QStringLiteral("正在启动 system-managerd..."), 2500);
        QTimer::singleShot(500, this, [this] { pollManager(); });
    }

    void stopManager()
    {
        if (!managerProcess_ || managerProcess_->state() == QProcess::NotRunning) {
            statusBar()->showMessage(QStringLiteral("检测服务不是由本上位启动，未执行停止"), 4000);
            return;
        }
        managerProcess_->terminate();
        if (!managerProcess_->waitForFinished(2000)) managerProcess_->kill();
        pollManager();
    }

    void showScriptEditor(qlonglong editingTaskId = 0, bool readOnly = false)
    {
        QDialog dialog(this);
        dialog.setWindowTitle(readOnly ? QStringLiteral("查看 Python 脚本配置") :
            (editingTaskId ? QStringLiteral("编辑 Python 脚本及绑定点") : QStringLiteral("新建 Python 脚本及绑定点")));
        dialog.resize(880, 560);
        auto *layout = new QVBoxLayout(&dialog);

        auto *basic = new QGroupBox(QStringLiteral("脚本任务"));
        auto *form = new QFormLayout(basic);
        auto *taskName = new QLineEdit(QStringLiteral("new_python_task"));
        auto *scriptFile = new QLineEdit(QStringLiteral("new_python_task.py"));
        auto *entryFunction = new QLineEdit(QStringLiteral("on_timer"));
        auto *triggerType = new QComboBox;
        triggerType->addItems({QStringLiteral("PERIODIC"), QStringLiteral("POINT_CHANGE"), QStringLiteral("MANUAL")});
        auto *interval = new QSpinBox;
        interval->setRange(10, 3600000);
        interval->setValue(1000);
        interval->setSuffix(QStringLiteral(" ms"));
        form->addRow(QStringLiteral("任务名称:"), taskName);
        form->addRow(QStringLiteral("脚本文件:"), scriptFile);
        form->addRow(QStringLiteral("入口函数:"), entryFunction);
        form->addRow(QStringLiteral("触发方式:"), triggerType);
        form->addRow(QStringLiteral("执行周期:"), interval);
        layout->addWidget(basic);

        auto *pointBar = new QHBoxLayout;
        pointBar->addWidget(new QLabel(QStringLiteral("绑定点配置（脚本别名用于 ctx.read/ctx.write）")));
        pointBar->addStretch();
        auto *addPoint = new QPushButton(QStringLiteral("增加点"));
        auto *removePoint = new QPushButton(QStringLiteral("删除点"));
        auto *importPoints = new QPushButton(QStringLiteral("CSV 导入..."));
        auto *exportPoints = new QPushButton(QStringLiteral("CSV 导出..."));
        pointBar->addWidget(addPoint);
        pointBar->addWidget(removePoint);
        pointBar->addWidget(importPoints);
        pointBar->addWidget(exportPoints);
        layout->addLayout(pointBar);

        auto *points = new QTableWidget(0, 7);
        points->setHorizontalHeaderLabels({QStringLiteral("系统点ID"), QStringLiteral("点名"),
            QStringLiteral("数据类型"), QStringLiteral("方向"), QStringLiteral("脚本别名"),
            QStringLiteral("访问方式"), QStringLiteral("初始值")});
        points->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        points->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        points->setSelectionBehavior(QAbstractItemView::SelectRows);
        layout->addWidget(points);

        auto appendPoint = [points](qlonglong pointId, int systemId, const QString &name,
                                    const QString &typeName, const QString &direction,
                                    const QString &access, const QString &alias,
                                    const QString &initialValue) {
            const int row = points->rowCount();
            points->insertRow(row);
            auto *idItem = new QTableWidgetItem(QString::number(systemId));
            idItem->setData(Qt::UserRole, pointId);
            points->setItem(row, 0, idItem);
            points->setItem(row, 1, new QTableWidgetItem(name));
            auto *type = new QComboBox;
            type->addItems({QStringLiteral("BOOL"), QStringLiteral("INT32"), QStringLiteral("UINT32"),
                            QStringLiteral("INT64"), QStringLiteral("UINT64"),
                            QStringLiteral("FLOAT32"), QStringLiteral("FLOAT64")});
            type->setCurrentText(typeName);
            points->setCellWidget(row, 2, type);
            auto *directionBox = new QComboBox;
            directionBox->addItems({QStringLiteral("INPUT"), QStringLiteral("OUTPUT"), QStringLiteral("BIDIRECTIONAL")});
            directionBox->setCurrentText(direction);
            points->setCellWidget(row, 3, directionBox);
            points->setItem(row, 4, new QTableWidgetItem(alias));
            auto *accessBox = new QComboBox;
            accessBox->addItems({QStringLiteral("READ"), QStringLiteral("WRITE"), QStringLiteral("READ_WRITE")});
            accessBox->setCurrentText(access);
            points->setCellWidget(row, 5, accessBox);
            points->setItem(row, 6, new QTableWidgetItem(initialValue));
        };
        if (!editingTaskId) {
            appendPoint(0, 2001, QStringLiteral("new_input"), QStringLiteral("FLOAT64"),
                        QStringLiteral("INPUT"), QStringLiteral("READ"), QStringLiteral("new_input"), QStringLiteral("0.0"));
            appendPoint(0, 2002, QStringLiteral("new_output"), QStringLiteral("FLOAT64"),
                        QStringLiteral("OUTPUT"), QStringLiteral("WRITE"), QStringLiteral("new_output"), QStringLiteral("0.0"));
        }
        connect(addPoint, &QPushButton::clicked, &dialog, [points, appendPoint] {
            const QString name = QStringLiteral("point_%1").arg(points->rowCount() + 1);
            appendPoint(0, 2001 + points->rowCount(), name, QStringLiteral("FLOAT64"),
                        QStringLiteral("INPUT"), QStringLiteral("READ"), name, QStringLiteral("0.0"));
        });
        connect(removePoint, &QPushButton::clicked, &dialog, [points] {
            const int row = points->currentRow();
            if (row >= 0) points->removeRow(row);
        });
        connect(importPoints, &QPushButton::clicked, &dialog, [&, points, appendPoint] {
            const QString path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("导入绑定点 CSV"),
                QString(), QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
            if (path.isEmpty()) return;
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox::critical(&dialog, QStringLiteral("导入失败"), input.errorString());
                return;
            }
            QString content = QString::fromUtf8(input.readAll());
            if (!content.isEmpty() && content.front() == QChar(0xfeff)) content.remove(0, 1);
            QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
            if (lines.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("导入失败"), QStringLiteral("CSV 文件为空。"));
                return;
            }
            bool headerOk = false;
            const QStringList header = CsvUtils::parseRow(lines.takeFirst(), &headerOk);
            const QStringList expected = {QStringLiteral("system_point_id"), QStringLiteral("name"),
                QStringLiteral("data_type"), QStringLiteral("direction"), QStringLiteral("alias"),
                QStringLiteral("access_mode"), QStringLiteral("initial_value")};
            if (!headerOk || header != expected) {
                QMessageBox::warning(&dialog, QStringLiteral("导入失败"),
                    QStringLiteral("CSV 表头必须为：\n%1").arg(expected.join(QLatin1Char(','))));
                return;
            }
            struct CsvPoint { int systemId; QString name, type, direction, alias, access, initial; };
            QList<CsvPoint> imported;
            QSet<int> ids;
            QSet<QString> names, aliases;
            const QSet<QString> types = {QStringLiteral("BOOL"), QStringLiteral("INT32"), QStringLiteral("UINT32"),
                QStringLiteral("INT64"), QStringLiteral("UINT64"), QStringLiteral("FLOAT32"), QStringLiteral("FLOAT64")};
            const QSet<QString> directions = {QStringLiteral("INPUT"), QStringLiteral("OUTPUT"), QStringLiteral("BIDIRECTIONAL")};
            const QSet<QString> accesses = {QStringLiteral("READ"), QStringLiteral("WRITE"), QStringLiteral("READ_WRITE")};
            for (int index = 0; index < lines.size(); ++index) {
                bool rowOk = false;
                const QStringList values = CsvUtils::parseRow(lines[index], &rowOk);
                bool idOk = false;
                const int systemId = values.value(0).trimmed().toInt(&idOk);
                const QString name = values.value(1).trimmed();
                const QString type = values.value(2).trimmed().toUpper();
                const QString direction = values.value(3).trimmed().toUpper();
                const QString alias = values.value(4).trimmed();
                const QString access = values.value(5).trimmed().toUpper();
                if (!rowOk || values.size() != 7 || !idOk || systemId <= 0 || name.isEmpty() || alias.isEmpty() ||
                        !types.contains(type) || !directions.contains(direction) || !accesses.contains(access) ||
                        ids.contains(systemId) || names.contains(name) || aliases.contains(alias)) {
                    QMessageBox::warning(&dialog, QStringLiteral("导入失败"),
                        QStringLiteral("第 %1 个数据行字段无效或存在重复 ID、点名、别名。" ).arg(index + 1));
                    return;
                }
                ids.insert(systemId); names.insert(name); aliases.insert(alias);
                imported.append({systemId, name, type, direction, alias, access, values.value(6).trimmed()});
            }
            QHash<int, qlonglong> existingIds;
            for (int row = 0; row < points->rowCount(); ++row)
                existingIds.insert(points->item(row, 0)->text().toInt(), points->item(row, 0)->data(Qt::UserRole).toLongLong());
            points->setRowCount(0);
            for (const CsvPoint &point : imported)
                appendPoint(existingIds.value(point.systemId, 0), point.systemId, point.name, point.type,
                    point.direction, point.access, point.alias, point.initial);
            QMessageBox::information(&dialog, QStringLiteral("导入完成"),
                QStringLiteral("已载入 %1 个绑定点；点击“写入 SQLite”后正式保存。" ).arg(imported.size()));
        });
        connect(exportPoints, &QPushButton::clicked, &dialog, [&, points] {
            const QString path = QFileDialog::getSaveFileName(&dialog, QStringLiteral("导出绑定点 CSV"),
                taskName->text().trimmed() + QStringLiteral("_points.csv"), QStringLiteral("CSV 文件 (*.csv)"));
            if (path.isEmpty()) return;
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::critical(&dialog, QStringLiteral("导出失败"), output.errorString());
                return;
            }
            QByteArray csv("\xEF\xBB\xBFsystem_point_id,name,data_type,direction,alias,access_mode,initial_value\n");
            for (int row = 0; row < points->rowCount(); ++row) {
                const QStringList values = {points->item(row, 0)->text(), points->item(row, 1)->text(),
                    qobject_cast<QComboBox *>(points->cellWidget(row, 2))->currentText(),
                    qobject_cast<QComboBox *>(points->cellWidget(row, 3))->currentText(), points->item(row, 4)->text(),
                    qobject_cast<QComboBox *>(points->cellWidget(row, 5))->currentText(), points->item(row, 6)->text()};
                QStringList escaped;
                for (const QString &value : values) escaped << CsvUtils::quote(value);
                csv += escaped.join(QLatin1Char(',')).toUtf8() + '\n';
            }
            output.write(csv);
            if (!output.commit())
                QMessageBox::critical(&dialog, QStringLiteral("导出失败"), output.errorString());
        });

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("写入 SQLite"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        const QString connectionName = QStringLiteral("script_editor_%1").arg(reinterpret_cast<quintptr>(&dialog));
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(currentDatabase_);

        if (editingTaskId) {
            if (!database.open()) {
                QMessageBox::critical(this, QStringLiteral("数据库错误"), database.lastError().text());
                database = QSqlDatabase();
                QSqlDatabase::removeDatabase(connectionName);
                return;
            }
            QSqlQuery load(database);
            load.prepare(QStringLiteral("SELECT name,script_path,entry_function,trigger_type,interval_ms FROM python_task WHERE id=?"));
            load.addBindValue(editingTaskId);
            if (!load.exec() || !load.next()) {
                QMessageBox::warning(this, QStringLiteral("任务不存在"), QStringLiteral("任务可能已被删除，请刷新工程。"));
                database.close();
                database = QSqlDatabase();
                QSqlDatabase::removeDatabase(connectionName);
                return;
            }
            taskName->setText(load.value(0).toString());
            scriptFile->setText(load.value(1).toString());
            entryFunction->setText(load.value(2).toString());
            triggerType->setCurrentText(load.value(3).toString());
            interval->setValue(load.value(4).toInt());
            load.prepare(QStringLiteral(
                "SELECT p.id,p.system_point_id,p.name,p.data_type,p.direction,b.access_mode,b.alias,p.initial_value "
                "FROM python_binding b JOIN point p ON p.id=b.point_id WHERE b.task_id=? ORDER BY b.id"));
            load.addBindValue(editingTaskId);
            if (load.exec()) while (load.next())
                appendPoint(load.value(0).toLongLong(), load.value(1).toInt(), load.value(2).toString(),
                            load.value(3).toString(), load.value(4).toString(), load.value(5).toString(),
                            load.value(6).toString(), load.value(7).toString());
            database.close();
        }

        if (readOnly) {
            taskName->setReadOnly(true);
            scriptFile->setReadOnly(true);
            entryFunction->setReadOnly(true);
            triggerType->setEnabled(false);
            interval->setEnabled(false);
            addPoint->setVisible(false);
            removePoint->setVisible(false);
            importPoints->setVisible(false);
            points->setEditTriggers(QAbstractItemView::NoEditTriggers);
            for (int row = 0; row < points->rowCount(); ++row)
                for (int column : {2, 3, 5}) points->cellWidget(row, column)->setEnabled(false);
            buttons->button(QDialogButtonBox::Save)->setVisible(false);
            buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("关闭"));
        }

        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            const QString task = taskName->text().trimmed();
            const QString file = scriptFile->text().trimmed();
            const QString entry = entryFunction->text().trimmed();
            if (task.isEmpty() || entry.isEmpty() || file.isEmpty() || points->rowCount() == 0) {
                QMessageBox::warning(&dialog, QStringLiteral("配置不完整"), QStringLiteral("任务、脚本、入口函数和至少一个点均为必填。"));
                return;
            }
            if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$")).match(entry).hasMatch()) {
                QMessageBox::warning(&dialog, QStringLiteral("入口函数无效"),
                    QStringLiteral("入口函数必须是合法的 Python 标识符。"));
                return;
            }
            if (QFileInfo(file).fileName() != file || !file.endsWith(QStringLiteral(".py"))) {
                QMessageBox::warning(&dialog, QStringLiteral("脚本文件无效"), QStringLiteral("脚本文件必须是当前脚本目录下的 .py 文件名。"));
                return;
            }
            QSet<QString> pointNames, aliases;
            for (int row = 0; row < points->rowCount(); ++row) {
                const QString name = points->item(row, 1) ? points->item(row, 1)->text().trimmed() : QString();
                const QString alias = points->item(row, 4) ? points->item(row, 4)->text().trimmed() : QString();
                bool idOk = false;
                const uint systemId = points->item(row, 0)->text().toUInt(&idOk);
                if (!idOk || !systemId || name.isEmpty() || alias.isEmpty() ||
                        pointNames.contains(name) || aliases.contains(alias) ||
                        alias.contains(QRegularExpression(QStringLiteral("[|;,\\r\\n]")))) {
                    QMessageBox::warning(&dialog, QStringLiteral("点配置无效"),
                        QStringLiteral("第 %1 行 ID、点名或别名无效，或者存在重复。" ).arg(row + 1));
                    return;
                }
                pointNames.insert(name);
                aliases.insert(alias);
            }
            if (!database.open()) {
                const QFileInfo databaseInfo(currentDatabase_);
                QMessageBox::critical(&dialog, QStringLiteral("数据库错误"),
                    QStringLiteral("无法打开工程数据库。\n\n路径：%1\n文件存在：%2\n目录可写：%3\n错误：%4")
                        .arg(currentDatabase_)
                        .arg(databaseInfo.exists() ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(databaseInfo.dir().isReadable() && QFileInfo(databaseInfo.dir().absolutePath()).isWritable()
                                 ? QStringLiteral("是") : QStringLiteral("否"))
                        .arg(database.lastError().text()));
                return;
            }
            const QString scriptPath = QDir(currentScriptsRoot_).filePath(file);
            bool createdScript = false;
            if (!QFileInfo::exists(scriptPath)) {
                QSaveFile output(scriptPath);
                if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QMessageBox::critical(&dialog, QStringLiteral("脚本错误"), output.errorString());
                    return;
                }
                QString skeleton = QStringLiteral(R"PY("""Python 通信任务模板。

点别名必须与上位机“绑定点配置”中的“脚本别名”一致。
Runner 会捕获 print、ctx.log 和异常信息，可在上位机“报文监视”中查看。
"""


def on_start(ctx):
    """可选生命周期函数：Runner 连接成功、脚本加载后执行一次。"""
    # ctx.task_id：当前脚本任务 ID；ctx.config_version：当前配置版本号。
    ctx.log("INFO", f"任务启动 task_id={ctx.task_id}")

    # POINT_CHANGE 任务会由 Runner 自动订阅所有可读绑定点，一般无需手工调用。
    # 如需主动订阅指定别名，可使用：
    # ctx.subscribe(["temperature", "pressure"])


def __ENTRY__(ctx, changes=None):
    """任务入口函数。

    PERIODIC（周期）或 MANUAL（手动）触发时，changes 为 None；
    POINT_CHANGE（点变化）触发时，changes 是变化点消息列表。
    """
    # 读取单点。返回 PointValue，可访问 value、data_type、status、
    # timestamp_ns 和 sequence。
    # temperature = ctx.read("temperature")
    # print("温度：", temperature.value, temperature.status)

    # 批量读取，返回 {别名: PointValue} 字典。
    # values = ctx.read_many(["temperature", "pressure"])
    # temp = values["temperature"].value
    # pressure = values["pressure"].value

    # 写入单点；只允许写入绑定为 WRITE 或 READ_WRITE 的点。
    # ctx.write("result", 12.5)

    # 批量写入。支持 bool、int、float，类型必须与 SQLite 点配置匹配。
    # ctx.write_many({"result": 12.5, "enable": True})

    # 写脚本调试日志。常用级别：DEBUG、INFO、WARN、ERROR。
    # 脚本日志显示在“报文监视”，不会重复写入 manager 运行日志。
    ctx.log("DEBUG", "入口函数执行")

    # print 的标准输出也会进入“报文监视”；适合临时调试。
    # print(f"temperature={temp}, pressure={pressure}")

    # 点变化触发时可查看变化消息；正式逻辑通常再通过 ctx.read() 读取类型化值。
    if changes:
        print("收到点变化：", changes)


def on_stop(ctx):
    """可选生命周期函数：任务正常停止前执行一次。"""
    ctx.log("INFO", "任务停止")
)PY");
                skeleton.replace(QStringLiteral("__ENTRY__"), entry);
                output.write(skeleton.toUtf8());
                if (!output.commit()) {
                    QMessageBox::critical(&dialog, QStringLiteral("脚本错误"), output.errorString());
                    return;
                }
                createdScript = true;
            }
            if (!database.transaction()) {
                QMessageBox::critical(&dialog, QStringLiteral("数据库错误"), database.lastError().text());
                return;
            }
            QSqlQuery query(database);
            bool ok = query.exec(QStringLiteral("SELECT id FROM config_version WHERE status='PUBLISHED'")) && query.next();
            const qlonglong versionId = ok ? query.value(0).toLongLong() : 0;
            qlonglong taskId = editingTaskId;
            if (editingTaskId) {
                query.prepare(QStringLiteral("UPDATE python_task SET name=?,script_path=?,entry_function=?,trigger_type=?,interval_ms=? WHERE id=?"));
                query.addBindValue(task); query.addBindValue(file); query.addBindValue(entry);
                query.addBindValue(triggerType->currentText()); query.addBindValue(interval->value());
                query.addBindValue(editingTaskId);
                ok = ok && query.exec();
                if (ok) {
                    query.prepare(QStringLiteral("DELETE FROM python_binding WHERE task_id=?"));
                    query.addBindValue(editingTaskId);
                    ok = query.exec();
                }
            } else {
                query.prepare(QStringLiteral("INSERT INTO python_task(version_id,name,script_path,entry_function,trigger_type,interval_ms,timeout_ms,restart_policy,enabled) VALUES(?,?,?,?,?,?,?,'ON_FAILURE',1)"));
                query.addBindValue(versionId); query.addBindValue(task); query.addBindValue(file);
                query.addBindValue(entry); query.addBindValue(triggerType->currentText());
                query.addBindValue(interval->value()); query.addBindValue(1000);
                ok = ok && query.exec();
                taskId = query.lastInsertId().toLongLong();
            }
            for (int row = 0; ok && row < points->rowCount(); ++row) {
                const QString name = points->item(row, 1)->text().trimmed();
                const QString alias = points->item(row, 4)->text().trimmed();
                const QString type = qobject_cast<QComboBox *>(points->cellWidget(row, 2))->currentText();
                const QString direction = qobject_cast<QComboBox *>(points->cellWidget(row, 3))->currentText();
                const QString access = qobject_cast<QComboBox *>(points->cellWidget(row, 5))->currentText();
                qlonglong pointId = points->item(row, 0)->data(Qt::UserRole).toLongLong();
                if (pointId) {
                    query.prepare(QStringLiteral("UPDATE point SET system_point_id=?,name=?,data_type=?,direction=?,initial_value=? WHERE id=?"));
                    query.addBindValue(points->item(row, 0)->text().toUInt()); query.addBindValue(name);
                    query.addBindValue(type); query.addBindValue(direction);
                    query.addBindValue(points->item(row, 6)->text().trimmed()); query.addBindValue(pointId);
                    ok = query.exec();
                } else {
                    query.prepare(QStringLiteral("INSERT INTO point(version_id,system_point_id,name,data_type,direction,initial_value,enabled) VALUES(?,?,?,?,?,?,1)"));
                    query.addBindValue(versionId); query.addBindValue(points->item(row, 0)->text().toUInt());
                    query.addBindValue(name); query.addBindValue(type); query.addBindValue(direction);
                    query.addBindValue(points->item(row, 6)->text().trimmed());
                    ok = query.exec();
                    pointId = query.lastInsertId().toLongLong();
                }
                if (ok) {
                    query.prepare(QStringLiteral("INSERT INTO python_binding(task_id,point_id,alias,access_mode,deadband,enabled) VALUES(?,?,?,?,0,1)"));
                    query.addBindValue(taskId); query.addBindValue(pointId); query.addBindValue(alias); query.addBindValue(access);
                    ok = query.exec();
                }
            }
            if (ok && editingTaskId)
                ok = query.exec(QStringLiteral("DELETE FROM point WHERE id NOT IN (SELECT point_id FROM python_binding)"));
            if (ok) {
                query.prepare(QStringLiteral("UPDATE config_version SET content_hash=? WHERE id=?"));
                query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
                query.addBindValue(versionId);
                ok = query.exec();
            }
            if (!ok || !database.commit()) {
                database.rollback();
                if (createdScript) QFile::remove(scriptPath);
                QMessageBox::critical(&dialog, QStringLiteral("写入失败"), query.lastError().text());
                return;
            }
            QMessageBox::information(&dialog, QStringLiteral("写入完成"),
                QStringLiteral("脚本和 %1 个绑定点已写入 SQLite。重新启动检测后生效。" ).arg(points->rowCount()));
            rebuildProjectTree();
            dialog.accept();
        });

        dialog.exec();
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }

    void deleteScriptTask(qlonglong taskId)
    {
        if (QMessageBox::question(this, QStringLiteral("删除脚本任务"),
                QStringLiteral("确定删除该任务、绑定关系和不再使用的点吗？\n磁盘上的 .py 脚本文件将保留。"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const QString connectionName = QStringLiteral("delete_task_%1").arg(QCoreApplication::applicationPid());
        bool ok = false;
        QString error;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(currentDatabase_);
            if (db.open() && db.transaction()) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral("DELETE FROM python_binding WHERE task_id=?"));
                query.addBindValue(taskId);
                ok = query.exec();
                if (ok) {
                    query.prepare(QStringLiteral("DELETE FROM python_task WHERE id=?"));
                    query.addBindValue(taskId);
                    ok = query.exec() && query.numRowsAffected() == 1;
                }
                if (ok) ok = query.exec(QStringLiteral("DELETE FROM point WHERE id NOT IN (SELECT point_id FROM python_binding)"));
                if (ok) {
                    query.prepare(QStringLiteral("UPDATE config_version SET content_hash=? WHERE status='PUBLISHED'"));
                    query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
                    ok = query.exec();
                }
                if (ok) ok = db.commit();
                else { error = query.lastError().text(); db.rollback(); }
                db.close();
            } else error = db.lastError().text();
        }
        QSqlDatabase::removeDatabase(connectionName);
        if (!ok) QMessageBox::critical(this, QStringLiteral("删除失败"), error);
        else {
            rebuildProjectTree();
            statusBar()->showMessage(QStringLiteral("任务已删除；重新启动检测后生效"), 4000);
        }
    }

    void showPointDirectory()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("全工程点目录"));
        dialog.resize(980, 560);
        auto *layout = new QVBoxLayout(&dialog);
        auto *filter = new QLineEdit;
        filter->setPlaceholderText(QStringLiteral("筛选点名、系统点 ID 或任务"));
        layout->addWidget(filter);
        auto *table = new QTableWidget(0, 8);
        table->setHorizontalHeaderLabels({QStringLiteral("点ID"), QStringLiteral("系统点ID"),
            QStringLiteral("点名"), QStringLiteral("数据类型"), QStringLiteral("方向"),
            QStringLiteral("初始值"), QStringLiteral("脚本别名"), QStringLiteral("所属任务")});
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
        layout->addWidget(table);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
        layout->addWidget(close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        const QString connectionName = QStringLiteral("point_directory_%1").arg(reinterpret_cast<quintptr>(&dialog));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(currentDatabase_);
            if (db.open()) {
                QSqlQuery query(db);
                query.exec(QStringLiteral(
                    "SELECT p.id,p.system_point_id,p.name,p.data_type,p.direction,p.initial_value,"
                    "COALESCE(GROUP_CONCAT(b.alias),''),COALESCE(GROUP_CONCAT(t.name),'') "
                    "FROM point p LEFT JOIN python_binding b ON b.point_id=p.id "
                    "LEFT JOIN python_task t ON t.id=b.task_id GROUP BY p.id ORDER BY p.system_point_id"));
                while (query.next()) {
                    const int row = table->rowCount();
                    table->insertRow(row);
                    for (int column = 0; column < table->columnCount(); ++column)
                        table->setItem(row, column, new QTableWidgetItem(query.value(column).toString()));
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connectionName);
        connect(filter, &QLineEdit::textChanged, table, [table](const QString &text) {
            for (int row = 0; row < table->rowCount(); ++row) {
                bool match = text.isEmpty();
                for (int column = 0; !match && column < table->columnCount(); ++column)
                    match = table->item(row, column)->text().contains(text, Qt::CaseInsensitive);
                table->setRowHidden(row, !match);
            }
        });
        dialog.exec();
    }

QWidget *createOnlineDataPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *hint = new QLabel(
        QStringLiteral(
            " 在线数据来自 ModbusCollector 共享内存 /modbus_data，每秒刷新一次。"
        )
    );

    hint->setStyleSheet(
        QStringLiteral(
            "background:#ffffc0;"
            "border:1px solid #808000;"
            "padding:4px;"
        )
    );

    layout->addWidget(hint);


    // =========================================================
    // Modbus 在线数据表
    // =========================================================

    onlineTable_ = new QTableWidget(0, 10);

    onlineTable_->setHorizontalHeaderLabels({
        QStringLiteral("点ID"),
        QStringLiteral("点名"),
        QStringLiteral("Slave ID"),
        QStringLiteral("功能码"),
        QStringLiteral("地址"),
        QStringLiteral("数据类型"),
        QStringLiteral("实时值"),
        QStringLiteral("质量"),
        QStringLiteral("采集时间"),
        QStringLiteral("Modbus 原始报文")
    });


    onlineTable_->setAlternatingRowColors(true);

    onlineTable_->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    onlineTable_->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    onlineTable_->setContextMenuPolicy(
        Qt::CustomContextMenu
    );


    // =========================================================
    // 列宽
    // =========================================================

    onlineTable_->setColumnWidth(0, 70);   // 点ID
    onlineTable_->setColumnWidth(1, 150);  // 点名
    onlineTable_->setColumnWidth(2, 80);   // Slave ID
    onlineTable_->setColumnWidth(3, 80);   // 功能码
    onlineTable_->setColumnWidth(4, 80);   // 地址
    onlineTable_->setColumnWidth(5, 100);  // 数据类型
    onlineTable_->setColumnWidth(6, 120);  // 实时值
    onlineTable_->setColumnWidth(7, 80);   // 质量
    onlineTable_->setColumnWidth(8, 190);  // 时间
    onlineTable_->setColumnWidth(9, 420);  // 原始报文


    // 点名可以自动扩展
    onlineTable_->horizontalHeader()->setSectionResizeMode(
        1,
        QHeaderView::Stretch
    );

    // 原始报文也允许扩展
    onlineTable_->horizontalHeader()->setSectionResizeMode(
        9,
        QHeaderView::Stretch
    );


    // =========================================================
    // 把表格加入页面
    // =========================================================

    layout->addWidget(onlineTable_);


    // =========================================================
    // 右键菜单
    // =========================================================

    connect(
        onlineTable_,
        &QTableWidget::customContextMenuRequested,
        this,
        [this](const QPoint &position)
        {
            const int row =
                onlineTable_->rowAt(position.y());

            if (row < 0)
                return;

            QMenu menu(onlineTable_);

            auto *force =
                menu.addAction(
                    pixelIcon(QColor("#800000"), 2),
                    QStringLiteral("强制值...")
                );

            if (
                menu.exec(
                    onlineTable_->viewport()
                        ->mapToGlobal(position)
                ) == force
            )
            {
                forceOnlineValue(row);
            }
        }
    );


    return page;
}



    void forceOnlineValue(int row)
    {
        if (!onlineTable_ || row < 0 || row >= onlineTable_->rowCount()) return;
        const QString pointId =
    onlineTable_->item(row, 0)->text();

const QString name =
    onlineTable_->item(row, 1)->text();

const QString type =
    onlineTable_->item(row, 5)->text();

const QString oldValue =
    onlineTable_->item(row, 6)->text();
        bool accepted = false;
        const QString value = QInputDialog::getText(this, QStringLiteral("强制点值"),
            QStringLiteral("点：%1（%2）\n输入强制值：").arg(name, type), QLineEdit::Normal,
            oldValue, &accepted).trimmed();
        if (!accepted || value.isEmpty()) return;
        if (QMessageBox::warning(this, QStringLiteral("确认强制"),
                QStringLiteral("将通过 system-managerd 修改共享内存中的点值：\n%1 = %2\n\n该操作会写入运行日志。")
                    .arg(name, value), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        const QJsonObject response = queryManager(3, &error, QJsonObject{
            {QStringLiteral("pointId"), pointId.toInt()},
            {QStringLiteral("value"), value},
            {QStringLiteral("configHash"), currentConfigHash()}
        });
        if (!response.value(QStringLiteral("ok")).toBool()) {
            QMessageBox::critical(this, QStringLiteral("强制失败"),
                response.value(QStringLiteral("message")).toString(
                    response.value(QStringLiteral("error")).toString(error)));
            return;
        }
        statusBar()->showMessage(QStringLiteral("已强制 %1 = %2").arg(name, value), 4000);
        pollManager();
    }

    void showOnlineData()
{
    for (int index = 0; index < tabs_->count(); ++index)
    {
        if (tabs_->tabText(index) == QStringLiteral("在线数据"))
        {
            onlineTabIndex_ = index;
            tabs_->setCurrentIndex(index);

            if (currentCommunicationType_ == QStringLiteral("modbus"))
                pollModbusSharedMemory();
            else
                pollManager();

            return;
        }
    }

    QWidget *page = createOnlineDataPage();

    onlineTabIndex_ =
        tabs_->addTab(
            page,
            pixelIcon(QColor("#008080"), 3),
            QStringLiteral("在线数据")
        );

    tabs_->setCurrentIndex(
        onlineTabIndex_
    );

    if (currentCommunicationType_ == QStringLiteral("modbus"))
        pollModbusSharedMemory();
    else
        pollManager();
}

    void showOverview()
    {
        for (int index = 0; index < tabs_->count(); ++index) {
            if (tabs_->tabText(index) == QStringLiteral("系统总览")) {
                tabs_->setCurrentIndex(index);
                return;
            }
        }
        QWidget *page = createOverviewPage();
        const int index = tabs_->addTab(page, pixelIcon(QColor("#000080"), 3), QStringLiteral("系统总览"));
        tabs_->setCurrentIndex(index);
        if (!currentDatabase_.isEmpty()) rebuildProjectTree();
    }

    void showFrameMonitor()
    {
        for (int index = 0; index < tabs_->count(); ++index) {
            if (tabs_->tabText(index) == QStringLiteral("报文监视")) {
                tabs_->setCurrentIndex(index);
                return;
            }
        }
        QWidget *page = createFramePage();
        const int index = tabs_->addTab(page, pixelIcon(QColor("#800080"), 3), QStringLiteral("报文监视"));
        tabs_->setCurrentIndex(index);
    }

void pollModbusSharedMemory()
{
    if (!onlineTable_)
        return;


    // =========================================================
    // 从 ModbusCollector 读取共享内存
    // =========================================================

    QVector<ModbusSharedVariable> variables;

    QString error;


    if (!modbusSharedMemoryReader_.read(
            variables,
            &error))
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Modbus共享内存未连接：%1"
            ).arg(error)
        );

        return;
    }


    // =========================================================
    // 更新在线数据表
    // =========================================================

    onlineTable_->setRowCount(
        variables.size()
    );


    for (int row = 0;
         row < variables.size();
         ++row)
    {
        const auto &var =
            variables.at(row);


        // =====================================================
        // 点ID
        // =====================================================

        onlineTable_->setItem(
            row,
            0,
            new QTableWidgetItem(
                QString::number(row + 1)
            )
        );


        // =====================================================
        // 点名
        // =====================================================

        onlineTable_->setItem(
            row,
            1,
            new QTableWidgetItem(
                var.name
            )
        );


        // =====================================================
        // Slave ID
        // =====================================================

        onlineTable_->setItem(
            row,
            2,
            new QTableWidgetItem(
                QString::number(
                    var.slave
                )
            )
        );


        // =====================================================
        // Modbus 功能码
        // =====================================================

        QString functionText =
            QStringLiteral(
                "%1"
            ).arg(var.function);


        // 给常见功能码增加说明
        if (var.function == 1)
        {
            functionText =
                QStringLiteral("01 (Read Coils)");
        }
        else if (var.function == 2)
        {
            functionText =
                QStringLiteral("02 (Read Discrete Inputs)");
        }
        else if (var.function == 3)
        {
            functionText =
                QStringLiteral("03 (Read Holding Registers)");
        }
        else if (var.function == 4)
        {
            functionText =
                QStringLiteral("04 (Read Input Registers)");
        }


        onlineTable_->setItem(
            row,
            3,
            new QTableWidgetItem(
                functionText
            )
        );


        // =====================================================
        // Modbus 地址
        // =====================================================

        onlineTable_->setItem(
            row,
            4,
            new QTableWidgetItem(
                QString::number(
                    var.address
                )
            )
        );


        // =====================================================
        // 数据类型
        // =====================================================

        onlineTable_->setItem(
            row,
            5,
            new QTableWidgetItem(
                var.datatype
            )
        );


        // =====================================================
        // 实时值
        // =====================================================

        QString valueText;

        if (
            var.datatype.compare(
                QStringLiteral("UINT16"),
                Qt::CaseInsensitive
            ) == 0 ||
            var.datatype.compare(
                QStringLiteral("INT16"),
                Qt::CaseInsensitive
            ) == 0
        )
        {
            valueText =
                QString::number(
                    var.value,
                    'f',
                    0
                );
        }
        else
        {
            valueText =
                QString::number(
                    var.value,
                    'f',
                    6
                );
        }


        auto *valueItem =
            new QTableWidgetItem(
                valueText
            );

        valueItem->setTextAlignment(
            Qt::AlignRight |
            Qt::AlignVCenter
        );

        onlineTable_->setItem(
            row,
            6,
            valueItem
        );


        // =====================================================
        // 通信质量
        // =====================================================

        const bool good =
            (var.status == 0);


        auto *statusItem =
            new QTableWidgetItem(
                good
                    ? QStringLiteral("Good")
                    : QStringLiteral("Bad")
            );


        statusItem->setTextAlignment(
            Qt::AlignCenter
        );


        statusItem->setForeground(
            good
                ? QColor("#008000")
                : QColor("#c00000")
        );


        onlineTable_->setItem(
            row,
            7,
            statusItem
        );


        // =====================================================
        // 采集时间
        // =====================================================

        onlineTable_->setItem(
            row,
            8,
            new QTableWidgetItem(
                var.timestamp
            )
        );


        // =====================================================
        // Modbus 原始报文
        //
        // 这里显示 ModbusCollector 保存的 telegram
        //
        // 例如：
        //
        // TX:
        // 00 01 00 00 00 06 01 03 00 00 00 01
        //
        // RX:
        // 00 01 00 00 00 05 01 03 02 30 39
        // =====================================================

        auto *telegramItem =
            new QTableWidgetItem(
                var.telegram
            );


        telegramItem->setToolTip(
            var.telegram
        );


        onlineTable_->setItem(
            row,
            9,
            telegramItem
        );
    }


    // =========================================================
    // 状态栏
    // =========================================================

    statusBar()->showMessage(
        QStringLiteral(
            "ModbusCollector：已读取 %1 个变量"
        ).arg(
            variables.size()
        )
    );
}



    void pollManager()
{
    // =========================================================
    // Modbus 工程使用 ModbusCollector + POSIX Shared Memory
    // 不使用 system-managerd 的实时数据 Snapshot
    // =========================================================

    if (currentCommunicationType_ == QStringLiteral("modbus"))
    {
        return;
    }
    QString error;

    // =========================================================
    // 1. 检查 system-managerd 是否在线
    // =========================================================

    const QJsonObject ping = queryManager(1, &error);

    const bool connected =
        !ping.isEmpty() &&
        ping.value(QStringLiteral("ok")).toBool();

    startManagerAction_->setEnabled(!connected);

    stopManagerAction_->setEnabled(
        managerProcess_ &&
        managerProcess_->state() != QProcess::NotRunning
    );

    if (!connected) {

        serviceStatus_->setText(
            QStringLiteral("● 检测服务断开")
        );

        serviceStatus_->setStyleSheet(
            QStringLiteral(
                "color:#c00000;font-weight:bold;"
            )
        );

        if (!error.isEmpty()) {
            serviceStatus_->setToolTip(
                QStringLiteral(
                    "连接 system-managerd 失败：%1"
                ).arg(error)
            );
        }

        return;
    }


    // =========================================================
    // 2. 检查配置是否匹配
    // =========================================================

    const QString managerHash =
        ping.value(
            QStringLiteral("configHash")
        ).toString();

    const QString projectHash =
        currentConfigHash();

    if (
        !managerHash.isEmpty() &&
        !projectHash.isEmpty() &&
        managerHash != projectHash
    ) {

        serviceStatus_->setText(
            QStringLiteral("● 已连接其他工程")
        );

        serviceStatus_->setStyleSheet(
            QStringLiteral(
                "color:#c00000;font-weight:bold;"
            )
        );

        startManagerAction_->setEnabled(false);

        return;
    }


    // =========================================================
    // 3. 显示服务状态
    // =========================================================

    serviceStatus_->setText(
        QStringLiteral(
            "● 已连接 system-managerd  PID %1  V%2"
        )
        .arg(
            ping.value(
                QStringLiteral("managerPid")
            ).toInt()
        )
        .arg(
            ping.value(
                QStringLiteral("configVersion")
            ).toInt()
        )
    );

    serviceStatus_->setStyleSheet(
        QStringLiteral(
            "color:#008000;font-weight:bold;"
        )
    );


    // =========================================================
    // 4. 没有在线数据表则结束
    // =========================================================

    if (!onlineTable_)
        return;


    // =========================================================
    // 5. 请求实时数据 Snapshot
    // =========================================================

    error.clear();

    const QJsonObject snapshot =
        queryManager(2, &error);


    if (snapshot.isEmpty()) {

        if (!error.isEmpty()) {

            serviceStatus_->setToolTip(
                QStringLiteral(
                    "获取实时数据失败：%1"
                ).arg(error)
            );
        }

        return;
    }


    if (
        !snapshot.value(
            QStringLiteral("ok")
        ).toBool()
    ) {

        serviceStatus_->setToolTip(
            QStringLiteral(
                "system-managerd 返回异常"
            )
        );

        return;
    }


    // =========================================================
    // 6. 获取 points 数组
    // =========================================================

    const QJsonArray points =
        snapshot.value(
            QStringLiteral("points")
        ).toArray();


    // =========================================================
    // 7. 更新 Qt 在线数据表
    // =========================================================

    onlineTable_->setRowCount(points.size());


    for (
        int row = 0;
        row < points.size();
        ++row
    ) {

        const QJsonObject point =
            points.at(row).toObject();


        // -----------------------------------------------------
        // 时间戳
        // -----------------------------------------------------

        const QString rawTimestamp =
            point.value(
                QStringLiteral("timestampNs")
            ).toString();


        bool timestampOk = false;

        const qint64 timestampNs =
            rawTimestamp.toLongLong(
                &timestampOk
            );


        QString displayTimestamp;


        if (timestampOk) {

            displayTimestamp =
                QDateTime::fromMSecsSinceEpoch(
                    timestampNs / 1000000
                ).toString(
                    QStringLiteral(
                        "yyyy-MM-dd HH:mm:ss.zzz"
                    )
                );

        } else {

            displayTimestamp =
                rawTimestamp;
        }


        // -----------------------------------------------------
        // 获取字段
        // -----------------------------------------------------

        const QString pointId =
            QString::number(
                point.value(
                    QStringLiteral("id")
                ).toInt()
            );


        const QString systemPointId =
            QString::number(
                point.value(
                    QStringLiteral("systemPointId")
                ).toInt()
            );


        const QString name =
            point.value(
                QStringLiteral("name")
            ).toString();


        const QString dataType =
            point.value(
                QStringLiteral("dataType")
            ).toString();


        const QString direction =
            point.value(
                QStringLiteral("direction")
            ).toString();


        const QString value =
            point.value(
                QStringLiteral("value")
            ).toString();


        const QString status =
            point.value(
                QStringLiteral("status")
            ).toString();


        const QString sequence =
            point.value(
                QStringLiteral("sequence")
            ).toString();


        // -----------------------------------------------------
        // 写入表格
        // -----------------------------------------------------

        const QStringList values = {

            pointId,
            systemPointId,
            name,
            dataType,
            direction,
            value,
            status,
            displayTimestamp,
            sequence
        };


        for (
            int column = 0;
            column < values.size();
            ++column
        ) {

            auto *item =
                new QTableWidgetItem(
                    values[column]
                );


            // =================================================
            // 状态颜色
            // =================================================

            if (column == 6) {

                if (
                    status ==
                    QStringLiteral("Good")
                ) {

                    item->setForeground(
                        QColor("#008000")
                    );

                } else if (
                    status ==
                    QStringLiteral("Bad")
                ) {

                    item->setForeground(
                        QColor("#c00000")
                    );

                } else {

                    item->setForeground(
                        QColor("#808080")
                    );
                }
            }


            // =================================================
            // 实时值颜色
            // =================================================

            if (column == 5) {

                if (
                    status ==
                    QStringLiteral("Good")
                ) {

                    item->setForeground(
                        QColor("#0040a0")
                    );
                }
            }


            // =================================================
            // 时间戳 Tooltip
            // =================================================

            if (
                column == 7 &&
                timestampOk
            ) {

                item->setToolTip(
                    QStringLiteral(
                        "原始时间戳：%1 ns"
                    ).arg(
                        rawTimestamp
                    )
                );
            }


            onlineTable_->setItem(
                row,
                column,
                item
            );
        }
    }
}

    void pollPythonDebug()
    {
        if (!frames_ && !overview_) return;
        int runningCount = 0;
        for (auto task = debugTasks_.cbegin(); task != debugTasks_.cend(); ++task) {
            const qlonglong taskId = task.key();
            const quint16 port = static_cast<quint16>(20000 + (taskId % 40000));
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            const bool running = socket.waitForConnected(100);
            setTaskRuntimeState(taskId, running, port);
            if (!running) continue;
            ++runningCount;
            if (!frames_ || framePaused_) continue;
            socket.write(QStringLiteral("GET %1\n").arg(debugCursors_.value(taskId, 0)).toUtf8());
            if (!socket.waitForBytesWritten(40) || !socket.waitForReadyRead(100)) continue;
            QByteArray response = socket.readAll();
            while (socket.waitForReadyRead(10)) response += socket.readAll();
            const QList<QByteArray> lines = response.split('\n');
            for (const QByteArray &line : lines) {
                if (line.trimmed().isEmpty()) continue;
                const QJsonDocument document = QJsonDocument::fromJson(line);
                if (!document.isObject()) continue;
                const QJsonObject entry = document.object();
                if (entry.value(QStringLiteral("type")).toString() == QStringLiteral("END")) {
                    debugCursors_[taskId] = qlonglong(entry.value(QStringLiteral("cursor")).toDouble());
                    continue;
                }
                appendPythonOutput(task.value(), entry);
            }
        }
        if (taskSummary_)
            taskSummary_->setText(QStringLiteral("%1 / %2").arg(runningCount).arg(debugTasks_.size()));
    }

    void setTaskRuntimeState(qlonglong taskId, bool running, quint16 port)
    {
        if (!overview_) return;
        for (int row = 0; row < overview_->rowCount(); ++row) {
            QTableWidgetItem *state = overview_->item(row, 0);
            if (!state || state->data(Qt::UserRole).toLongLong() != taskId) continue;
            state->setText(running ? QStringLiteral("● 运行") : QStringLiteral("○ 离线"));
            state->setForeground(running ? QColor("#008000") : QColor("#c00000"));
            if (overview_->item(row, 3))
                overview_->item(row, 3)->setText(running
                    ? QStringLiteral("调试 TCP 127.0.0.1:%1").arg(port)
                    : QStringLiteral("本机共享内存"));
            if (overview_->item(row, 6))
                overview_->item(row, 6)->setText(running ? QStringLiteral("运行正常") : QStringLiteral("待启动"));
            break;
        }
    }

    void appendPythonOutput(const QString &taskName, const QJsonObject &entry)
    {
        const QString text = entry.value(QStringLiteral("text")).toString();
        const QString stream = entry.value(QStringLiteral("stream")).toString();
        QString summary = text.simplified();
        constexpr int summaryLimit = 160;
        if (summary.size() > summaryLimit)
            summary = summary.left(summaryLimit) + QStringLiteral("…");
        const qint64 timestampNs = qint64(entry.value(QStringLiteral("timestampNs")).toDouble());
        const QDateTime timestamp = QDateTime::fromMSecsSinceEpoch(timestampNs / 1000000);
        frames_->insertRow(0);
        const QStringList values = {
            timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), QStringLiteral("PY"), taskName,
            summary, QString::number(text.toUtf8().size()), stream
        };
        for (int column = 0; column < values.size(); ++column)
            frames_->setItem(0, column, new QTableWidgetItem(values[column]));
        frames_->item(0, 3)->setData(Qt::UserRole, text);
        frames_->item(0, 3)->setToolTip(QStringLiteral("单击查看完整内容"));
        frames_->item(0, 1)->setForeground(QColor("#800080"));
        if (stream == QStringLiteral("stderr")) {
            frames_->item(0, 3)->setForeground(QColor("#c00000"));
            frames_->item(0, 5)->setForeground(QColor("#c00000"));
        }
        while (frames_->rowCount() > 500) frames_->removeRow(frames_->rowCount() - 1);
    }
};

} // namespace

QMainWindow *createMainWindow(const QStringList &arguments)
{
    auto *window = new MainWindow;
    const int projectArg = arguments.indexOf(QStringLiteral("--project"));
    if (projectArg >= 0 && projectArg + 1 < arguments.size())
        window->openProjectPath(arguments.at(projectArg + 1));
    const bool integrationPreview = arguments.contains(QStringLiteral("--integration-preview"));
    if (integrationPreview)
        QTimer::singleShot(100, window, [window] { window->openIntegrationPreview(); });
    if (arguments.contains(QStringLiteral("--script-editor-preview")))
        QTimer::singleShot(100, window, [window] { window->openScriptEditorPreview(); });
    const int screenshotArg = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotArg >= 0 && screenshotArg + 1 < arguments.size()) {
        const QString path = arguments.at(screenshotArg + 1);
        QTimer::singleShot(integrationPreview ? 1800 : 500, window, [window, path] {
            QWidget *target = QApplication::activeWindow();
            (target ? target : window)->grab().save(path);
            QApplication::quit();
        });
    }
    return window;
}
