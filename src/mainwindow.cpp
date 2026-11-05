#include "mainwindow.h"
#include "retro_style.h"
#include "manager_client.h"
#include "csv_utils.h"
#include "project_metadata.h"
#include "modbus_server_client.h"

#include <QtWidgets>
#include <QtNetwork>
#include <QtSql>
#include <QHash>
#include <QComboBox>
#include <QSqlError>

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
        if (modbusCollectorProcess_ && modbusCollectorProcess_->state() != QProcess::NotRunning) {
            modbusCollectorProcess_->terminate();
            if (!modbusCollectorProcess_->waitForFinished(1500)) modbusCollectorProcess_->kill();
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
    QHash<QString, QString> lastModbusFrameKeys_;
    QPlainTextEdit *frameDetail_ = nullptr;
    QComboBox *frameSourceFilter_ = nullptr;
    QComboBox *frameDirectionFilter_ = nullptr;
    QComboBox *frameFunctionFilter_ = nullptr;
    QPlainTextEdit *runtimeLog_ = nullptr;
    QTableWidget *problems_ = nullptr;
    QTabWidget *logTabs_ = nullptr;
    QTableWidget *onlineTable_ = nullptr;
    QTableWidget *modbusPointsTable_ = nullptr;
    QTimer *modbusTimer_ = nullptr;
    QString currentCommunicationType_ =
    QStringLiteral("python");

    
    QLabel *clock_ = nullptr;
    QLabel *rxRate_ = nullptr;
    QLabel *serviceStatus_ = nullptr;
    QLabel *projectVersionLabel_ = nullptr;
    QLabel *taskSummary_ = nullptr;
    QLabel *pointSummary_ = nullptr;
    QLabel *currentProjectLabel_ = nullptr;
    QTreeWidget *projectTree_ = nullptr;

    QTreeWidgetItem *projectRootItem_ = nullptr;

    QTreeWidgetItem *pythonCommunicationItem_ = nullptr;
    QTreeWidgetItem *pythonTasksItem_ = nullptr;

    QTreeWidgetItem *modbusCommunicationItem_ = nullptr;
    QTreeWidgetItem *modbusTasksItem_ = nullptr;
    QTreeWidgetItem *modbusPointsItem_ = nullptr;

    QLineEdit *modbusTaskNameEdit_ = nullptr;

    QLineEdit *modbusTaskHostEdit_ = nullptr;
    QLineEdit *modbusTaskPortEdit_ = nullptr;
    QLineEdit *modbusTaskSlaveEdit_ = nullptr;

    QLineEdit *modbusTaskPeriodEdit_ = nullptr;
    QLineEdit *modbusTaskTimeoutEdit_ = nullptr;
    QLineEdit *modbusTaskRetryEdit_ = nullptr;
    QLineEdit *modbusTaskReconnectEdit_ = nullptr;

    QComboBox *modbusTaskProtocolEdit_ = nullptr;
    QLineEdit *modbusTaskRoleEdit_ = nullptr;
    QLineEdit *modbusTaskSerialPortEdit_ = nullptr;
    QComboBox *modbusTaskBaudEdit_ = nullptr;
    QComboBox *modbusTaskParityEdit_ = nullptr;
    QComboBox *modbusTaskDataBitsEdit_ = nullptr;
    QComboBox *modbusTaskStopBitsEdit_ = nullptr;
    QLabel *modbusTaskHostLabel_ = nullptr;
    QLabel *modbusTaskPortLabel_ = nullptr;
    QLabel *modbusTaskSerialPortLabel_ = nullptr;
    QLabel *modbusTaskBaudLabel_ = nullptr;
    QLabel *modbusTaskParityLabel_ = nullptr;
    QLabel *modbusTaskDataBitsLabel_ = nullptr;
    QLabel *modbusTaskStopBitsLabel_ = nullptr;

    QPushButton *saveModbusTaskButton_ = nullptr;

    // 当前正在编辑/查看的 Modbus 采集任务
    qlonglong currentModbusTaskId_ = -1;
    QTreeWidgetItem *pointDirectoryItem_ = nullptr;
    QAction *startManagerAction_ = nullptr;
    QAction *stopManagerAction_ = nullptr;
        QProcess *managerProcess_ = nullptr;
    QProcess *modbusCollectorProcess_ = nullptr;
    int onlineTabIndex_ = -1;

    // ModbusCollector TCP Server
    //
    // ModbusCollector:
    //     127.0.0.1:19091
    //
    // MainWindow 通过 ModbusServerClient
    // 请求实时变量和 Modbus TX/RX 报文。
    ModbusServerClient modbusServerClient_;
    quint32 modbusRequestId_ = 0;

    quint32 requestId_ = 0;
    QString currentProjectDirectory_;
    QString currentProjectName_;
    QString currentDatabase_;
    QString modbusConfigDatabase_ =
    QStringLiteral("/home/xu/桌面/ModbusCollector/modbus_config.db");
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
        startManagerAction_->setToolTip(
    QStringLiteral(
        "启动当前选中的通信通道检测"
    )
);

stopManagerAction_->setToolTip(
    QStringLiteral(
        "停止当前选中的通信通道检测"
    )
);
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
                frameSourceFilter_ = nullptr; frameDirectionFilter_ = nullptr; frameFunctionFilter_ = nullptr;
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


    // 搜索框

    auto *filter = new QLineEdit;

    filter->setPlaceholderText(
        QStringLiteral("查找工程对象")
    );

    layout->addWidget(filter);


    // 工程树

    auto *tree = new QTreeWidget;

    projectTree_ = tree;

    tree->setHeaderLabel(
        QStringLiteral("对象名称")
    );

    tree->setAnimated(false);


    auto add =
        [&](QTreeWidgetItem *parent,
            const QString &name,
            QColor color,
            int kind)
    {
        auto *item =
            parent
                ? new QTreeWidgetItem(parent)
                : new QTreeWidgetItem(tree);

        item->setText(0, name);

        item->setIcon(
            0,
            pixelIcon(color, kind)
        );

        return item;
    };


    // 工程根节点

    auto *root =
        add(
            nullptr,
            QString(),
            QColor("#c8a000"),
            0
        );

    projectRootItem_ = root;

    root->setData(
        0,
        Qt::UserRole,
        QStringLiteral("project_root")
    );

    root->setHidden(true);


    // 采集设备

    auto *channels =
        add(
            root,
            QStringLiteral("采集设备"),
            QColor("#000080"),
            3
        );

    channels->setData(
        0,
        Qt::UserRole,
        QStringLiteral("channels")
    );


    // Python 通信

    auto *python =
        add(
            channels,
            QStringLiteral("Python 通信"),
            QColor("#008080"),
            3
        );

    pythonCommunicationItem_ = python;

    python->setData(
        0,
        Qt::UserRole,
        QStringLiteral("python_communication")
    );


    auto *pythonTasks =
        add(
            python,
            QStringLiteral("脚本任务"),
            QColor("#008080"),
            1
        );

    pythonTasksItem_ = pythonTasks;

    pythonTasks->setData(
        0,
        Qt::UserRole,
        QStringLiteral("python_tasks")
    );


    // Modbus 通信

    auto *modbus =
        add(
            channels,
            QStringLiteral("Modbus 通信"),
            QColor("#800080"),
            3
        );

    modbusCommunicationItem_ = modbus;

    modbus->setData(
        0,
        Qt::UserRole,
        QStringLiteral("modbus_communication")
    );


    // Modbus 采集任务目录
    //
    // 实际的 Modbus任务1 / Modbus任务2 ...
    // 后面由 rebuildProjectTree() 从 modbus_tasks 动态创建。

    auto *modbusTasks =
        add(
            modbusCommunicationItem_,
            QStringLiteral("采集任务"),
            QColor("#800080"),
            1
        );

    modbusTasksItem_ = modbusTasks;

    modbusTasksItem_->setData(
        0,
        Qt::UserRole,
        QStringLiteral("modbus_tasks")
    );


    // 点表节点以后属于具体任务，不再直接属于 Modbus 通信
    modbusPointsItem_ = nullptr;

    modbusCommunicationItem_->setExpanded(true);
    modbusTasksItem_->setExpanded(true);


    // 点目录

    pointDirectoryItem_ =
        add(
            root,
            QStringLiteral("点目录"),
            QColor("#c8a000"),
            0
        );

    pointDirectoryItem_->setData(
        0,
        Qt::UserRole,
        QStringLiteral("point_directory")
    );


    // 展开状态

    root->setExpanded(true);

    channels->setExpanded(true);

    python->setExpanded(true);


    layout->addWidget(tree);


    // 搜索

    connect(
        filter,
        &QLineEdit::textChanged,
        tree,
        [tree](const QString &text)
        {
            for (
                auto *item :
                tree->findItems(
                    QStringLiteral("*"),
                    Qt::MatchWildcard |
                    Qt::MatchRecursive
                )
            )
            {
                item->setHidden(
                    !text.isEmpty()
                    &&
                    !item->text(0).contains(
                        text,
                        Qt::CaseInsensitive
                    )
                );
            }
        }
    );


    // 当前选中通信类型
    //
    // Python 通信
    //      ↓
    // currentCommunicationType_ = python
    //
    // Modbus 通信
    //      ↓
    // currentCommunicationType_ = modbus

    connect(
        tree,
        &QTreeWidget::currentItemChanged,
        this,
        [this](
            QTreeWidgetItem *current,
            QTreeWidgetItem *
        )
        {
            if (!current)
                return;

            const QString kind =
                current->data(
                    0,
                    Qt::UserRole
                ).toString();


            if (
                kind
                ==
                QStringLiteral(
                    "python_communication"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("python");

                statusBar()->showMessage(
                    QStringLiteral(
                        "当前选择：Python 通信。点击“启动检测”可启动 Python 通信检测。"
                    ),
                    3000
                );
            }


            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_communication"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("modbus");

                statusBar()->showMessage(
                    QStringLiteral(
                        "当前选择：Modbus 通信。点击“启动检测”可启动 Modbus 通信检测。"
                    ),
                    3000
                );
            }

            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_tasks"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("modbus");

                statusBar()->showMessage(
                    QStringLiteral(
                        "当前选择：Modbus 采集任务。"
                    ),
                    3000
                );
            }


            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_task"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("modbus");

                currentModbusTaskId_ =
                    current->data(
                        0,
                        Qt::UserRole + 1
                    ).toLongLong();

                loadCurrentModbusTask();

                statusBar()->showMessage(
                    QStringLiteral(
                        "当前选择：Modbus 采集任务 %1。"
                    )
                    .arg(
                        currentModbusTaskId_
                    ),
                    3000
                );
            }

            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_points"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("modbus");

                currentModbusTaskId_ =
                    current->data(
                        0,
                        Qt::UserRole + 1
                    ).toLongLong();


                loadCurrentModbusTask();

                showModbusPoints();


                statusBar()->showMessage(
                    QStringLiteral(
                        "当前选择：Modbus 任务 %1 的点表。"
                    )
                    .arg(
                        currentModbusTaskId_
                    ),
                    3000
                );
            }
        }
    );


    // 右键菜单

    tree->setContextMenuPolicy(
        Qt::CustomContextMenu
    );


    connect(
        tree,
        &QTreeWidget::customContextMenuRequested,
        this,
        [this, tree](const QPoint &position)
        {
            auto *item =
                tree->itemAt(position);

            if (!item)
                return;


            const QString kind =
                item->data(
                    0,
                    Qt::UserRole
                ).toString();


            QMenu menu(tree);


            if (
                kind
                ==
                QStringLiteral("project_root")
            )
            {
                auto *rename =
                    menu.addAction(
                        QStringLiteral(
                            "修改工程名称..."
                        )
                    );

                if (
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(position)
                    )
                    ==
                    rename
                )
                {
                    renameProject();
                }
            }


            else if (
                kind
                ==
                QStringLiteral("python_tasks")
            )
            {
                auto *create =
                    menu.addAction(
                        pixelIcon(
                            QColor("#008080"),
                            1
                        ),
                        QStringLiteral(
                            "新建脚本..."
                        )
                    );

                if (
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(position)
                    )
                    ==
                    create
                )
                {
                    showScriptEditor();
                }
            }


            else if (
                kind
                ==
                QStringLiteral("python_task")
            )
            {
                auto *view =
                    menu.addAction(
                        QStringLiteral("查看配置")
                    );

                auto *edit =
                    menu.addAction(
                        QStringLiteral("编辑配置...")
                    );

                menu.addSeparator();

                auto *remove =
                    menu.addAction(
                        pixelIcon(
                            QColor("#800000"),
                            2
                        ),
                        QStringLiteral("删除任务...")
                    );


                QAction *selected =
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(position)
                    );


                const qlonglong taskId =
                    item->data(
                        0,
                        Qt::UserRole + 1
                    ).toLongLong();


                if (selected == view)
                {
                    showScriptEditor(
                        taskId,
                        true
                    );
                }
                else if (selected == edit)
                {
                    showScriptEditor(
                        taskId,
                        false
                    );
                }
                else if (selected == remove)
                {
                    deleteScriptTask(taskId);
                }
            }

            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_tasks"
                )
            )
            {
                auto *createTask =
                    menu.addAction(
                        pixelIcon(
                            QColor("#008000"),
                            4
                        ),
                        QStringLiteral(
                            "新建 Modbus 任务"
                        )
                    );


                QAction *selected =
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(
                                position
                            )
                    );


                if (
                    selected
                    ==
                    createTask
                )
                {
                    createNewModbusTask();
                }
            }

                else if (
                    kind
                    ==
                    QStringLiteral(
                        "modbus_task"
                    )
                )
                {
                    auto *editTask =
                        menu.addAction(
                            QStringLiteral(
                                "编辑任务"
                            )
                        );


                    menu.addSeparator();


                    auto *deleteTask =
                        menu.addAction(
                            QStringLiteral(
                                "删除任务"
                            )
                        );


                    QAction *selected =
                        menu.exec(
                            tree->viewport()
                                ->mapToGlobal(
                                    position
                                )
                        );


                    // 编辑任务

                    if (
                        selected
                        ==
                        editTask
                    )
                    {
                        currentCommunicationType_ =
                            QStringLiteral("modbus");

                        currentModbusTaskId_ =
                            item->data(
                                0,
                                Qt::UserRole + 1
                            )
                            .toLongLong();


                        tree->setCurrentItem(
                            item
                        );


                        loadCurrentModbusTask();


                        statusBar()->showMessage(
                            QStringLiteral(
                                "正在编辑 Modbus 任务 %1。"
                            )
                            .arg(
                                currentModbusTaskId_
                            ),
                            2500
                        );
                    }


                    // 删除任务

                    else if (
                        selected
                        ==
                        deleteTask
                    )
                    {
                        const qlonglong taskId =
                            item->data(
                                0,
                                Qt::UserRole + 1
                            )
                            .toLongLong();


                        deleteModbusTask(
                            taskId
                        );
                    }
                }

            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_communication"
                )
            )
            {
                auto *start =
                    menu.addAction(
                        pixelIcon(
                            QColor("#008000"),
                            4
                        ),
                        QStringLiteral(
                            "启动 Modbus 检测"
                        )
                    );

                auto *stop =
                    menu.addAction(
                        pixelIcon(
                            QColor("#800000"),
                            2
                        ),
                        QStringLiteral(
                            "停止 Modbus 检测"
                        )
                    );


                QAction *selected =
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(position)
                    );


                if (selected == start)
                {
                    currentCommunicationType_ =
                        QStringLiteral("modbus");

                    startManager();
                }
                else if (selected == stop)
                {
                    currentCommunicationType_ =
                        QStringLiteral("modbus");

                    stopManager();
                }
            }


            else if (
                kind
                ==
                QStringLiteral(
                    "point_directory"
                )
            )
            {
                auto *view =
                    menu.addAction(
                        QStringLiteral(
                            "查看点目录"
                        )
                    );

                if (
                    menu.exec(
                        tree->viewport()
                            ->mapToGlobal(position)
                    )
                    ==
                    view
                )
                {
                    showPointDirectory();
                }
            }
        }
    );


    // 双击

    connect(
        tree,
        &QTreeWidget::itemDoubleClicked,
        this,
        [this](QTreeWidgetItem *item)
        {
            if (!item)
                return;


            const QString kind =
                item->data(
                    0,
                    Qt::UserRole
                ).toString();


            if (
                kind
                ==
                QStringLiteral("python_tasks")
            )
            {
                showScriptEditor();
            }


            else if (
                kind
                ==
                QStringLiteral("python_task")
            )
            {
                showScriptEditor(
                    item->data(
                        0,
                        Qt::UserRole + 1
                    ).toLongLong(),
                    false
                );
            }


            else if (
                kind
                ==
                QStringLiteral(
                    "modbus_communication"
                )
            )
            {
                currentCommunicationType_ =
                    QStringLiteral("modbus");
            }


            else if (
                kind
                ==
                QStringLiteral(
                    "point_directory"
                )
            )
            {
                showPointDirectory();
            }
        }
    );


    return box;
}

QWidget *createModbusPointsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    // 顶部标题

    auto *title =
        new QLabel(
            QStringLiteral(" Modbus 点表配置")
        );

    title->setStyleSheet(
        QStringLiteral(
            "background:#000080;"
            "color:white;"
            "font-weight:bold;"
            "padding:5px;"
        )
    );

    layout->addWidget(title);


    // 工具栏

    auto *bar =
        new QHBoxLayout;

    auto *addButton =
        new QPushButton(
            QStringLiteral("新建点")
        );

    auto *editButton =
        new QPushButton(
            QStringLiteral("修改点")
        );

    auto *deleteButton =
        new QPushButton(
            QStringLiteral("删除点")
        );

    auto *saveButton =
        new QPushButton(
            QStringLiteral("保存点表")
        );

    bar->addWidget(addButton);
    bar->addWidget(editButton);
    bar->addWidget(deleteButton);

    bar->addSpacing(12);

    bar->addWidget(saveButton);

    bar->addStretch();

    auto *hint =
        new QLabel(
            QStringLiteral(
                "修改后点击“保存点表”，"
                "再点击顶部“启动检测”使配置生效"
            )
        );

    hint->setStyleSheet(
        QStringLiteral(
            "color:#000080;"
        )
    );

    bar->addWidget(hint);

    layout->addLayout(bar);


    // 点表

    modbusPointsTable_ =
        new QTableWidget(
            0,
            5
        );

    modbusPointsTable_->setHorizontalHeaderLabels({
        QStringLiteral("点名"),
        QStringLiteral("Modbus地址"),
        QStringLiteral("读写方向"),
        QStringLiteral("数据类型"),
        QStringLiteral("大小端转换")
    });

    modbusPointsTable_->setAlternatingRowColors(true);

    modbusPointsTable_->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    modbusPointsTable_->setSelectionMode(
        QAbstractItemView::SingleSelection
    );

    // 点名和 Modbus 地址可直接双击编辑。
    modbusPointsTable_->setEditTriggers(
        QAbstractItemView::DoubleClicked
        |
        QAbstractItemView::EditKeyPressed
        |
        QAbstractItemView::SelectedClicked
    );

    modbusPointsTable_->horizontalHeader()
        ->setSectionResizeMode(
            0,
            QHeaderView::Stretch
        );

    modbusPointsTable_->setColumnWidth(
        1,
        110
    );

    modbusPointsTable_->setColumnWidth(
        2,
        110
    );

    modbusPointsTable_->setColumnWidth(
        3,
        110
    );

    modbusPointsTable_->setColumnWidth(
        4,
        120
    );

    layout->addWidget(
        modbusPointsTable_
    );


    // 第一阶段暂时只验证页面
    //
    // 下一阶段：
    // 新建 / 修改 / 删除 / 保存 -> SQLite

    connect(
    addButton,
    &QPushButton::clicked,
    this,
    [this]
    {
        if (!modbusPointsTable_)
            return;

        // 新点名称：test1 / test2 / test3 ...

        int testIndex = 1;

        while (true)
        {
            const QString candidate =
                QStringLiteral("test%1")
                .arg(testIndex);

            bool exists = false;

            for (
                int row = 0;
                row < modbusPointsTable_->rowCount();
                ++row
            )
            {
                QTableWidgetItem *item =
                    modbusPointsTable_->item(
                        row,
                        0
                    );

                if (
                    item
                    &&
                    item->text()
                    ==
                    candidate
                )
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
                break;

            ++testIndex;
        }

        const QString pointName =
            QStringLiteral("test%1")
            .arg(testIndex);


        // 自动生成下一个 Modbus 地址
        //
        // 默认从 40001 开始

        int nextAddress = 40001;

        for (
            int row = 0;
            row < modbusPointsTable_->rowCount();
            ++row
        )
        {
            QTableWidgetItem *addressItem =
                modbusPointsTable_->item(
                    row,
                    1
                );

            if (!addressItem)
                continue;

            bool ok = false;

            const int address =
                addressItem->text().toInt(&ok);

            if (
                ok
                &&
                address >= nextAddress
            )
            {
                nextAddress =
                    address + 1;
            }
        }


        // 新增表格行

        const int row =
            modbusPointsTable_->rowCount();

        modbusPointsTable_->insertRow(row);


        // 第0列：点名

        auto *nameItem =
            new QTableWidgetItem(
                pointName
            );

        // id = -1 表示这是新建点，还没有写入数据库
        nameItem->setData(
            Qt::UserRole,
            -1
        );

        // 默认 Slave ID = 1
        nameItem->setData(
            Qt::UserRole + 1,
            1
        );

        // 默认 function = 3
        nameItem->setData(
            Qt::UserRole + 2,
            3
        );

        // 默认原始地址
        nameItem->setData(
            Qt::UserRole + 3,
            nextAddress - 40001
        );

        modbusPointsTable_->setItem(
            row,
            0,
            nameItem
        );


        // 第1列：Modbus 地址

        auto *addressItem = new QTableWidgetItem(
            QString::number(nextAddress));
        addressItem->setToolTip(
            QStringLiteral(
                "可配置地址段：00001~09999、10001~19999、"
                "30001~39999、40001~49999"));
        modbusPointsTable_->setItem(row, 1, addressItem);


        // 第2列：读写方向
        // 默认 INPUT

        auto *directionCombo =
            new QComboBox;

        directionCombo->addItems({
            QStringLiteral("INPUT"),
            QStringLiteral("OUTPUT")
        });

        directionCombo->setCurrentText(
            QStringLiteral("INPUT")
        );

        modbusPointsTable_->setCellWidget(
            row,
            2,
            directionCombo
        );


        // 第3列：数据类型
        // 默认 INT16

        auto *datatypeCombo =
            new QComboBox;

        datatypeCombo->addItems({
            QStringLiteral("INT16"),
            QStringLiteral("UINT16"),
            QStringLiteral("INT32"),
            QStringLiteral("UINT32"),
            QStringLiteral("FLOAT"),
            QStringLiteral("DOUBLE")
        });

        datatypeCombo->setCurrentText(
            QStringLiteral("INT16")
        );

        modbusPointsTable_->setCellWidget(
            row,
            3,
            datatypeCombo
        );


        // 第4列：大小端
        // 默认 ABCD

        auto *byteOrderCombo =
            new QComboBox;

        byteOrderCombo->addItems({
            QStringLiteral("ABCD"),
            QStringLiteral("BADC"),
            QStringLiteral("CDAB"),
            QStringLiteral("DCBA")
        });

        byteOrderCombo->setCurrentText(
            QStringLiteral("ABCD")
        );

        modbusPointsTable_->setCellWidget(
            row,
            4,
            byteOrderCombo
        );


        // 自动选中新建行

        modbusPointsTable_->selectRow(
            row
        );

        modbusPointsTable_->scrollToItem(
            nameItem
        );

        statusBar()->showMessage(
            QStringLiteral(
                "已新建 Modbus 点：%1，可双击 Modbus地址自定义位置"
            )
            .arg(pointName),
            2000
        );
    }
);

    connect(
        editButton,
        &QPushButton::clicked,
        this,
        [this]
        {
            if (!modbusPointsTable_)
                return;

            const int row = modbusPointsTable_->currentRow();
            if (row < 0)
            {
                QMessageBox::information(
                    this,
                    QStringLiteral("修改 Modbus 点"),
                    QStringLiteral("请先选择要修改的点。"));
                return;
            }

            auto *nameItem = modbusPointsTable_->item(row, 0);
            auto *addressItem = modbusPointsTable_->item(row, 1);
            auto *directionCombo = qobject_cast<QComboBox *>(
                modbusPointsTable_->cellWidget(row, 2));
            if (!nameItem || !addressItem || !directionCombo)
                return;

            bool accepted = false;
            const QString addressText = QInputDialog::getText(
                this,
                QStringLiteral("修改 Modbus 位置"),
                QStringLiteral(
                    "点：%1\n"
                    "输入标准 Modbus 地址：\n"
                    "00001~09999  线圈\n"
                    "10001~19999  离散输入\n"
                    "30001~39999  输入寄存器\n"
                    "40001~49999  保持寄存器")
                    .arg(nameItem->text()),
                QLineEdit::Normal,
                addressItem->text(),
                &accepted).trimmed();

            if (!accepted)
                return;

            bool addressOk = false;
            const int address = addressText.toInt(&addressOk);
            const bool supported =
                (address >= 1 && address <= 9999)
                || (address >= 10001 && address <= 19999)
                || (address >= 30001 && address <= 39999)
                || (address >= 40001 && address <= 49999);

            if (!addressOk || !supported)
            {
                QMessageBox::warning(
                    this,
                    QStringLiteral("地址无效"),
                    QStringLiteral("请输入支持的标准 Modbus 地址。"));
                return;
            }

            if (directionCombo->currentText() == QStringLiteral("OUTPUT")
                && !(address >= 40001 && address <= 49999))
            {
                QMessageBox::warning(
                    this,
                    QStringLiteral("输出地址无效"),
                    QStringLiteral(
                        "OUTPUT 点必须使用可写的保持寄存器地址 "
                        "40001~49999。"));
                return;
            }

            addressItem->setText(QString::number(address));
            statusBar()->showMessage(
                QStringLiteral("已将点 %1 的 Modbus 地址修改为 %2，请点击“保存点表”")
                    .arg(nameItem->text(), QString::number(address)),
                3000);
        }
    );

    connect(
    deleteButton,
    &QPushButton::clicked,
    this,
    [this]
    {
        if (!modbusPointsTable_)
            return;

        // 获取当前选择的行

        const int row =
            modbusPointsTable_->currentRow();

        if (
            row < 0
            ||
            row >= modbusPointsTable_->rowCount()
        )
        {
            QMessageBox::information(
                this,
                QStringLiteral("删除 Modbus 点"),
                QStringLiteral(
                    "请先选择要删除的点。"
                )
            );

            return;
        }


        // 获取点名

        QTableWidgetItem *nameItem =
            modbusPointsTable_->item(
                row,
                0
            );

        const QString pointName =
            nameItem
            ?
            nameItem->text()
            :
            QStringLiteral("未命名点");


        // 删除前确认

        const QMessageBox::StandardButton answer =
            QMessageBox::question(
                this,
                QStringLiteral("删除 Modbus 点"),
                QStringLiteral(
                    "确定要删除点“%1”吗？\n\n"
                    "当前操作只修改点表，"
                    "点击“保存点表”后才会同步到 Modbus 配置数据库。"
                )
                .arg(pointName),
                QMessageBox::Yes
                |
                QMessageBox::No,
                QMessageBox::No
            );

        if (
            answer
            !=
            QMessageBox::Yes
        )
        {
            return;
        }


        // 从当前表格删除

        modbusPointsTable_->removeRow(
            row
        );


        // 删除后选择附近的一行

        if (
            modbusPointsTable_->rowCount()
            >
            0
        )
        {
            int newRow = row;

            if (
                newRow
                >=
                modbusPointsTable_->rowCount()
            )
            {
                newRow =
                    modbusPointsTable_->rowCount()
                    -
                    1;
            }

            modbusPointsTable_->selectRow(
                newRow
            );
        }


        statusBar()->showMessage(
            QStringLiteral(
                "已从当前点表删除 Modbus 点：%1"
            )
            .arg(pointName),
            2500
        );
    }
);

    connect(
    saveButton,
    &QPushButton::clicked,
    this,
    [this]
    {
        saveModbusPointsTable();
    }
);


    return page;
}

void loadCurrentModbusTask()
{
    if (
        currentModbusTaskId_
        <=
        0
    )
    {
        return;
    }


    const QString connectionName =
        QStringLiteral(
            "modbus_task_load_%1"
        )
        .arg(
            reinterpret_cast<quintptr>(
                this
            )
        );


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        if (!db.open())
        {
            QMessageBox::warning(
                this,
                QStringLiteral(
                    "Modbus 任务"
                ),
                QStringLiteral(
                    "无法打开 Modbus 配置数据库：\n%1"
                )
                .arg(
                    db.lastError().text()
                )
            );

            return;
        }


        QSqlQuery query(db);

        query.prepare(
            QStringLiteral(
                "SELECT "
                "name, "
                "host, "
                "port, "
                "slave_id, "
                "period, "
                "timeout, "
                "retry_count, "
                "reconnect_interval, "
                "enable, transport, serial_port, baud_rate, parity, data_bits, stop_bits "
                "FROM modbus_tasks "
                "WHERE id = ?"
            )
        );

        query.addBindValue(
            currentModbusTaskId_
        );


        if (
            query.exec()
            &&
            query.next()
        )
        {
            if (modbusTaskNameEdit_)
            {
                modbusTaskNameEdit_->setText(
                    query.value(0).toString()
                );
            }


            if (modbusTaskHostEdit_)
            {
                modbusTaskHostEdit_->setText(
                    query.value(1).toString()
                );
            }


            if (modbusTaskPortEdit_)
            {
                modbusTaskPortEdit_->setText(
                    QString::number(
                        query.value(2).toInt()
                    )
                );
            }


            if (modbusTaskSlaveEdit_)
            {
                modbusTaskSlaveEdit_->setText(
                    QString::number(
                        query.value(3).toInt()
                    )
                );
            }


            if (modbusTaskPeriodEdit_)
            {
                modbusTaskPeriodEdit_->setText(
                    QStringLiteral("%1 ms")
                    .arg(
                        query.value(4).toInt()
                    )
                );
            }


            if (modbusTaskTimeoutEdit_)
            {
                modbusTaskTimeoutEdit_->setText(
                    QStringLiteral("%1 ms")
                    .arg(
                        query.value(5).toInt()
                    )
                );
            }


            if (modbusTaskRetryEdit_)
            {
                modbusTaskRetryEdit_->setText(
                    QString::number(
                        query.value(6).toInt()
                    )
                );
            }


            if (modbusTaskReconnectEdit_)
            {
                modbusTaskReconnectEdit_->setText(
                    QStringLiteral("%1 ms")
                    .arg(
                        query.value(7).toInt()
                    )
                );
            }


            if (modbusTaskProtocolEdit_)
            {
                const QString transport = query.value(9).toString().trimmed().toUpper();
                modbusTaskProtocolEdit_->setCurrentText(
                    transport == QStringLiteral("RTU") ? QStringLiteral("Modbus RTU") : QStringLiteral("Modbus TCP"));
            }

            if (modbusTaskSerialPortEdit_) modbusTaskSerialPortEdit_->setText(query.value(10).toString());
            if (modbusTaskBaudEdit_) modbusTaskBaudEdit_->setCurrentText(QString::number(query.value(11).toInt()));
            if (modbusTaskParityEdit_) {
                const QString parity = query.value(12).toString().toUpper();
                modbusTaskParityEdit_->setCurrentIndex(parity == QStringLiteral("E") ? 1 : parity == QStringLiteral("O") ? 2 : 0);
            }
            if (modbusTaskDataBitsEdit_) modbusTaskDataBitsEdit_->setCurrentText(QString::number(query.value(13).toInt()));
            if (modbusTaskStopBitsEdit_) modbusTaskStopBitsEdit_->setCurrentText(QString::number(query.value(14).toInt()));


            if (modbusTaskRoleEdit_)
            {
                modbusTaskRoleEdit_->setText(
                    QStringLiteral(
                        "主站"
                    )
                );
            }
        }


        db.close();
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );
}

void saveCurrentModbusTask()
{
    if (
        currentModbusTaskId_
        <=
        0
    )
    {
        QMessageBox::information(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "请先在左侧选择一个 Modbus 采集任务。"
            )
        );

        return;
    }


    if (
        !modbusTaskNameEdit_
        ||
        !modbusTaskHostEdit_
        ||
        !modbusTaskPortEdit_
        ||
        !modbusTaskSlaveEdit_
        ||
        !modbusTaskPeriodEdit_
        ||
        !modbusTaskTimeoutEdit_
        ||
        !modbusTaskRetryEdit_
        ||
        !modbusTaskReconnectEdit_
        || !modbusTaskProtocolEdit_ || !modbusTaskSerialPortEdit_ || !modbusTaskBaudEdit_
        || !modbusTaskParityEdit_ || !modbusTaskDataBitsEdit_ || !modbusTaskStopBitsEdit_
    )
    {
        return;
    }


    const QString name =
        modbusTaskNameEdit_
        ->text()
        .trimmed();

    const QString host =
        modbusTaskHostEdit_
        ->text()
        .trimmed();

    const bool isRtu = modbusTaskProtocolEdit_->currentText().contains(QStringLiteral("RTU"));
    const QString transport = isRtu ? QStringLiteral("RTU") : QStringLiteral("TCP");
    const QString serialPort = modbusTaskSerialPortEdit_->text().trimmed();
    bool baudOk = false;
    const int baudRate = modbusTaskBaudEdit_->currentText().trimmed().toInt(&baudOk);
    const QString parity = QString(modbusTaskParityEdit_->currentText().isEmpty()
                                   ? QChar('N') : modbusTaskParityEdit_->currentText().at(0));
    const int dataBits = modbusTaskDataBitsEdit_->currentText().toInt();
    const int stopBits = modbusTaskStopBitsEdit_->currentText().toInt();


    bool portOk = false;
    bool slaveOk = false;
    bool periodOk = false;
    bool timeoutOk = false;
    bool retryOk = false;
    bool reconnectOk = false;


    const int port =
        modbusTaskPortEdit_
        ->text()
        .trimmed()
        .toInt(
            &portOk
        );

    const int slaveId =
        modbusTaskSlaveEdit_
        ->text()
        .trimmed()
        .toInt(
            &slaveOk
        );


    // 允许输入：
    // 1000
    // 或
    // 1000 ms

    QString periodText =
        modbusTaskPeriodEdit_
        ->text()
        .trimmed();

    periodText.remove(
        QStringLiteral("ms"),
        Qt::CaseInsensitive
    );

    const int period =
        periodText
        .trimmed()
        .toInt(
            &periodOk
        );


    QString timeoutText =
        modbusTaskTimeoutEdit_
        ->text()
        .trimmed();

    timeoutText.remove(
        QStringLiteral("ms"),
        Qt::CaseInsensitive
    );

    const int timeout =
        timeoutText
        .trimmed()
        .toInt(
            &timeoutOk
        );


    const int retryCount =
        modbusTaskRetryEdit_
        ->text()
        .trimmed()
        .toInt(
            &retryOk
        );


    QString reconnectText =
        modbusTaskReconnectEdit_
        ->text()
        .trimmed();

    reconnectText.remove(
        QStringLiteral("ms"),
        Qt::CaseInsensitive
    );

    const int reconnectInterval =
        reconnectText
        .trimmed()
        .toInt(
            &reconnectOk
        );


    // 参数检查

    if (name.isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "任务名称不能为空。"
            )
        );

        return;
    }


    if (!isRtu && host.isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "IP 地址不能为空。"
            )
        );

        return;
    }


    if (
        !isRtu && (!portOk
        ||
        port <= 0
        ||
        port > 65535)
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "端口必须在 1~65535 之间。"
            )
        );

        return;
    }

    if (isRtu && (serialPort.isEmpty() || !baudOk || baudRate <= 0))
    {
        QMessageBox::warning(this, QStringLiteral("保存 Modbus 任务"),
                             QStringLiteral("RTU 串口设备不能为空，波特率必须大于 0。"));
        return;
    }


    if (
        !slaveOk
        ||
        slaveId < 1
        ||
        slaveId > 247
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "Unit ID 必须在 1~247 之间。"
            )
        );

        return;
    }


    if (
        !periodOk
        ||
        period <= 0
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "采集周期必须大于 0 ms。"
            )
        );

        return;
    }


    if (
        !timeoutOk
        ||
        timeout <= 0
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "响应超时必须大于 0 ms。"
            )
        );

        return;
    }


    if (
        !retryOk
        ||
        retryCount < 0
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "重试次数不能小于 0。"
            )
        );

        return;
    }


    if (
        !reconnectOk
        ||
        reconnectInterval <= 0
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "重连间隔必须大于 0 ms。"
            )
        );

        return;
    }


    // 保存数据库

    const QString connectionName =
        QStringLiteral(
            "modbus_task_save_%1"
        )
        .arg(
            reinterpret_cast<quintptr>(
                this
            )
        );


    bool success = false;
    QString errorMessage;


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        if (db.open())
        {
            QSqlQuery query(db);

            query.prepare(
                QStringLiteral(
                    "UPDATE modbus_tasks "
                    "SET "
                    "name = ?, "
                    "host = ?, "
                    "port = ?, "
                    "slave_id = ?, "
                    "period = ?, "
                    "timeout = ?, "
                    "retry_count = ?, "
                    "reconnect_interval = ? "
                    ", transport = ?, serial_port = ?, baud_rate = ?, parity = ?, data_bits = ?, stop_bits = ? "
                    "WHERE id = ?"
                )
            );


            query.addBindValue(name);
            query.addBindValue(host);
            query.addBindValue(port);
            query.addBindValue(slaveId);
            query.addBindValue(period);
            query.addBindValue(timeout);
            query.addBindValue(retryCount);
            query.addBindValue(
                reconnectInterval
            );
            query.addBindValue(transport);
            query.addBindValue(serialPort);
            query.addBindValue(baudRate);
            query.addBindValue(parity);
            query.addBindValue(dataBits);
            query.addBindValue(stopBits);
            query.addBindValue(
                currentModbusTaskId_
            );


            if (query.exec())
            {
                success = true;


                // 过渡阶段：
                // 当前 Collector 仍然读 modbus_points.period，
                // 因此将任务周期同步给该任务下面的旧点字段。

                QSqlQuery pointQuery(db);

                pointQuery.prepare(
                    QStringLiteral(
                        "UPDATE modbus_points "
                        "SET period = ?, "
                        "slave_id = ? "
                        "WHERE task_id = ?"
                    )
                );

                pointQuery.addBindValue(
                    period
                );

                pointQuery.addBindValue(
                    slaveId
                );

                pointQuery.addBindValue(
                    currentModbusTaskId_
                );

                if (!pointQuery.exec())
                {
                    success = false;

                    errorMessage =
                        pointQuery
                        .lastError()
                        .text();
                }
            }
            else
            {
                errorMessage =
                    query.lastError().text();
            }


            db.close();
        }
        else
        {
            errorMessage =
                db.lastError().text();
        }
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );


    if (!success)
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "保存 Modbus 任务"
            ),
            QStringLiteral(
                "保存失败：\n%1"
            )
            .arg(
                errorMessage
            )
        );

        return;
    }


    // 统一格式重新显示
    modbusTaskPeriodEdit_->setText(
        QStringLiteral("%1 ms")
        .arg(period)
    );

    modbusTaskTimeoutEdit_->setText(
        QStringLiteral("%1 ms")
        .arg(timeout)
    );

    modbusTaskReconnectEdit_->setText(
        QStringLiteral("%1 ms")
        .arg(
            reconnectInterval
        )
    );


    // 任务名称可能发生变化，
    // 所以重新生成左侧任务树
    rebuildProjectTree();


    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 任务“%1”保存成功。"
        )
        .arg(name),
        3000
    );
}

void createNewModbusTask()
{
    const QString connectionName =
        QStringLiteral(
            "modbus_task_create_%1"
        )
        .arg(
            reinterpret_cast<quintptr>(
                this
            )
        );


    bool success = false;
    QString errorMessage;

    qlonglong newTaskId = -1;
    QString newTaskName;


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        // 打开数据库

        if (!db.open())
        {
            errorMessage =
                db.lastError().text();
        }
        else
        {
            // 自动寻找可用任务名
            //
            // 已有：
            // Modbus任务1
            // Modbus任务2
            //
            // 则自动创建：
            // Modbus任务3

            int taskIndex = 1;

            while (true)
            {
                const QString candidate =
                    QStringLiteral(
                        "Modbus任务%1"
                    )
                    .arg(taskIndex);


                QSqlQuery checkQuery(db);

                checkQuery.prepare(
                    QStringLiteral(
                        "SELECT COUNT(*) "
                        "FROM modbus_tasks "
                        "WHERE name = ?"
                    )
                );

                checkQuery.addBindValue(
                    candidate
                );


                bool exists = false;

                if (
                    checkQuery.exec()
                    &&
                    checkQuery.next()
                )
                {
                    exists =
                        checkQuery
                        .value(0)
                        .toInt()
                        >
                        0;
                }


                if (!exists)
                {
                    newTaskName =
                        candidate;

                    break;
                }


                ++taskIndex;
            }


            // 创建新的 Modbus 任务

            QSqlQuery insertQuery(db);

            insertQuery.prepare(
                QStringLiteral(
                    "INSERT INTO modbus_tasks "
                    "("
                    "name, "
                    "host, "
                    "port, "
                    "slave_id, "
                    "period, "
                    "timeout, "
                    "retry_count, "
                    "reconnect_interval, "
                    "enable"
                    ") "
                    "VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?)"
                )
            );


            insertQuery.addBindValue(
                newTaskName
            );

            insertQuery.addBindValue(
                QStringLiteral(
                    "127.0.0.1"
                )
            );

            insertQuery.addBindValue(
                502
            );

            insertQuery.addBindValue(
                1
            );

            // 默认采集周期
            insertQuery.addBindValue(
                1000
            );

            // 默认响应超时
            insertQuery.addBindValue(
                1000
            );

            // 默认重试次数
            insertQuery.addBindValue(
                2
            );

            // 默认重连间隔
            insertQuery.addBindValue(
                3000
            );

            // 默认启用
            insertQuery.addBindValue(
                1
            );


            if (insertQuery.exec())
            {
                newTaskId =
                    insertQuery
                    .lastInsertId()
                    .toLongLong();

                success = true;
            }
            else
            {
                errorMessage =
                    insertQuery
                    .lastError()
                    .text();
            }


            db.close();
        }
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );


    // 创建失败

    if (!success)
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "新建 Modbus 任务"
            ),
            QStringLiteral(
                "创建 Modbus 任务失败：\n%1"
            )
            .arg(
                errorMessage
            )
        );

        return;
    }


    // 当前任务切换到刚刚创建的任务

    currentCommunicationType_ =
        QStringLiteral("modbus");

    currentModbusTaskId_ =
        newTaskId;


    // 刷新左侧工程树

    rebuildProjectTree();


    // 在树中找到刚创建的任务并选中

    if (modbusTasksItem_)
    {
        for (
            int index = 0;
            index < modbusTasksItem_->childCount();
            ++index
        )
        {
            QTreeWidgetItem *taskItem =
                modbusTasksItem_->child(
                    index
                );

            if (!taskItem)
            {
                continue;
            }


            const qlonglong taskId =
                taskItem->data(
                    0,
                    Qt::UserRole + 1
                )
                .toLongLong();


            if (
                taskId
                ==
                newTaskId
            )
            {
                if (projectTree_)
                {
                    projectTree_->setCurrentItem(
                        taskItem
                    );
                }

                taskItem->setExpanded(
                    true
                );

                break;
            }
        }
    }


    // 右侧显示新任务配置

    loadCurrentModbusTask();


    statusBar()->showMessage(
        QStringLiteral(
            "已创建 Modbus 采集任务：%1"
        )
        .arg(
            newTaskName
        ),
        3000
    );
}

void deleteModbusTask(
    qlonglong taskId
)
{
    if (taskId <= 0)
    {
        return;
    }


    const QString connectionName =
        QStringLiteral(
            "modbus_task_delete_%1"
        )
        .arg(
            reinterpret_cast<quintptr>(
                this
            )
        );


    QString taskName;
    int pointCount = 0;
    QString errorMessage;


    // 第一阶段：
    // 查询任务名称和任务下面的点数

    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        if (!db.open())
        {
            errorMessage =
                db.lastError().text();
        }
        else
        {
            // 查询任务

            QSqlQuery taskQuery(db);

            taskQuery.prepare(
                QStringLiteral(
                    "SELECT name "
                    "FROM modbus_tasks "
                    "WHERE id = ?"
                )
            );

            taskQuery.addBindValue(
                taskId
            );


            if (
                !taskQuery.exec()
                ||
                !taskQuery.next()
            )
            {
                errorMessage =
                    QStringLiteral(
                        "找不到要删除的 Modbus 任务。"
                    );

                if (
                    taskQuery.lastError().isValid()
                )
                {
                    errorMessage +=
                        QStringLiteral(
                            "\n%1"
                        )
                        .arg(
                            taskQuery
                            .lastError()
                            .text()
                        );
                }
            }
            else
            {
                taskName =
                    taskQuery
                    .value(0)
                    .toString();


                // 统计该任务下面的点

                QSqlQuery countQuery(db);

                countQuery.prepare(
                    QStringLiteral(
                        "SELECT COUNT(*) "
                        "FROM modbus_points "
                        "WHERE task_id = ?"
                    )
                );

                countQuery.addBindValue(
                    taskId
                );


                if (
                    countQuery.exec()
                    &&
                    countQuery.next()
                )
                {
                    pointCount =
                        countQuery
                        .value(0)
                        .toInt();
                }
                else
                {
                    errorMessage =
                        QStringLiteral(
                            "统计任务点数失败：\n%1"
                        )
                        .arg(
                            countQuery
                            .lastError()
                            .text()
                        );
                }
            }


            db.close();
        }
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );


    if (!errorMessage.isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "删除 Modbus 任务"
            ),
            errorMessage
        );

        return;
    }


    // 第二阶段：
    // 让用户确认

    QString confirmText;


    if (pointCount > 0)
    {
        confirmText =
            QStringLiteral(
                "确定删除 Modbus 任务“%1”吗？\n\n"
                "该任务下面还有 %2 个 Modbus 点。\n"
                "继续删除将同时永久删除这些点。\n\n"
                "此操作无法撤销。"
            )
            .arg(taskName)
            .arg(pointCount);
    }
    else
    {
        confirmText =
            QStringLiteral(
                "确定删除 Modbus 任务“%1”吗？\n\n"
                "该任务当前没有 Modbus 点。\n\n"
                "此操作无法撤销。"
            )
            .arg(taskName);
    }


    const QMessageBox::StandardButton answer =
        QMessageBox::warning(
            this,
            QStringLiteral(
                "确认删除 Modbus 任务"
            ),
            confirmText,
            QMessageBox::Yes
            |
            QMessageBox::No,
            QMessageBox::No
        );


    if (
        answer
        !=
        QMessageBox::Yes
    )
    {
        return;
    }


    // 第三阶段：
    // transaction 中执行真正删除
    //
    // 1. DELETE modbus_points
    // 2. DELETE modbus_tasks
    // 3. COMMIT
    //
    // 任一步失败全部 ROLLBACK

    const QString deleteConnectionName =
        QStringLiteral(
            "modbus_task_delete_exec_%1"
        )
        .arg(
            reinterpret_cast<quintptr>(
                this
            )
        );


    bool success = false;
    QString deleteError;


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                deleteConnectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        if (!db.open())
        {
            deleteError =
                db.lastError().text();
        }
        else if (!db.transaction())
        {
            deleteError =
                QStringLiteral(
                    "无法启动数据库事务：\n%1"
                )
                .arg(
                    db.lastError().text()
                );
        }
        else
        {
            bool transactionOk = true;


            // 先删除这个任务下面所有点

            QSqlQuery deletePointsQuery(db);

            deletePointsQuery.prepare(
                QStringLiteral(
                    "DELETE FROM modbus_points "
                    "WHERE task_id = ?"
                )
            );

            deletePointsQuery.addBindValue(
                taskId
            );


            if (!deletePointsQuery.exec())
            {
                transactionOk = false;

                deleteError =
                    QStringLiteral(
                        "删除任务下的 Modbus 点失败：\n%1"
                    )
                    .arg(
                        deletePointsQuery
                        .lastError()
                        .text()
                    );
            }


            // 再删除任务本身

            if (transactionOk)
            {
                QSqlQuery deleteTaskQuery(db);

                deleteTaskQuery.prepare(
                    QStringLiteral(
                        "DELETE FROM modbus_tasks "
                        "WHERE id = ?"
                    )
                );

                deleteTaskQuery.addBindValue(
                    taskId
                );


                if (!deleteTaskQuery.exec())
                {
                    transactionOk = false;

                    deleteError =
                        QStringLiteral(
                            "删除 Modbus 任务失败：\n%1"
                        )
                        .arg(
                            deleteTaskQuery
                            .lastError()
                            .text()
                        );
                }
                else if (
                    deleteTaskQuery.numRowsAffected()
                    !=
                    1
                )
                {
                    transactionOk = false;

                    deleteError =
                        QStringLiteral(
                            "删除 Modbus 任务失败："
                            "数据库中没有找到对应任务。"
                        );
                }
            }


            // COMMIT / ROLLBACK

            if (transactionOk)
            {
                if (db.commit())
                {
                    success = true;
                }
                else
                {
                    deleteError =
                        QStringLiteral(
                            "提交删除事务失败：\n%1"
                        )
                        .arg(
                            db.lastError().text()
                        );

                    db.rollback();
                }
            }
            else
            {
                db.rollback();
            }


            db.close();
        }
    }


    QSqlDatabase::removeDatabase(
        deleteConnectionName
    );


    // 删除失败

    if (!success)
    {
        QMessageBox::warning(
            this,
            QStringLiteral(
                "删除 Modbus 任务"
            ),
            deleteError
        );

        return;
    }


    // 如果删掉的正好是当前任务
    // 清除当前任务选择

    if (
        currentModbusTaskId_
        ==
        taskId
    )
    {
        currentModbusTaskId_ =
            -1;


        // 清空当前点表
        if (modbusPointsTable_)
        {
            modbusPointsTable_
                ->setRowCount(0);
        }


        // 清空右侧任务属性
        if (modbusTaskNameEdit_)
            modbusTaskNameEdit_->clear();

        if (modbusTaskHostEdit_)
            modbusTaskHostEdit_->clear();

        if (modbusTaskPortEdit_)
            modbusTaskPortEdit_->clear();

        if (modbusTaskSlaveEdit_)
            modbusTaskSlaveEdit_->clear();

        if (modbusTaskPeriodEdit_)
            modbusTaskPeriodEdit_->clear();

        if (modbusTaskTimeoutEdit_)
            modbusTaskTimeoutEdit_->clear();

        if (modbusTaskRetryEdit_)
            modbusTaskRetryEdit_->clear();

        if (modbusTaskReconnectEdit_)
            modbusTaskReconnectEdit_->clear();
    }


    // 重建左侧工程树

    rebuildProjectTree();


    statusBar()->showMessage(
        QStringLiteral(
            "已删除 Modbus 任务“%1”，"
            "同时删除 %2 个 Modbus 点。"
        )
        .arg(taskName)
        .arg(pointCount),
        3500
    );
}

void loadModbusPointsTable()
{
    if (!modbusPointsTable_)
        return;

    modbusPointsTable_->setRowCount(0);

    const QString connectionName =
        QStringLiteral("modbus_points_%1")
        .arg(reinterpret_cast<quintptr>(this));

    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );

        // 打开数据库

        if (!db.open())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("Modbus 点表"),
                QStringLiteral(
                    "无法打开 Modbus 配置数据库：\n%1\n\n%2"
                )
                .arg(
                    modbusConfigDatabase_,
                    db.lastError().text()
                )
            );

            return;
        }


        // 查询 modbus_points

        QSqlQuery query(db);

// 检查 modbus_points 是否已经存在 direction 字段

bool hasDirectionColumn = false;

if (
    query.exec(
        QStringLiteral(
            "PRAGMA table_info(modbus_points)"
        )
    )
)
{
    while (query.next())
    {
        // PRAGMA table_info 返回的第 2 列是字段名
        if (
            query.value(1).toString()
            ==
            QStringLiteral("direction")
        )
        {
            hasDirectionColumn = true;
            break;
        }
    }
}


// 老数据库没有 direction 时自动增加

if (!hasDirectionColumn)
{
    if (
        !query.exec(
            QStringLiteral(
                "ALTER TABLE modbus_points "
                "ADD COLUMN direction TEXT "
                "DEFAULT 'INPUT'"
            )
        )
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Modbus 点表"),
            QStringLiteral(
                "增加 direction 字段失败：\n%1"
            )
            .arg(
                query.lastError().text()
            )
        );

        db.close();
        return;
    }
}

// Modbus 多任务数据库升级
//
// 新结构：
// modbus_tasks  -> 保存一个 Modbus 采集任务的公共配置
// modbus_points -> 通过 task_id 归属某个任务
//
// 当前阶段只做数据库兼容升级，暂时不修改现有 UI。


// 1. 创建 modbus_tasks 表

if (
    !query.exec(
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS modbus_tasks "
            "("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "host TEXT NOT NULL DEFAULT '127.0.0.1', "
            "port INTEGER NOT NULL DEFAULT 502, "
            "slave_id INTEGER NOT NULL DEFAULT 1, "
            "period INTEGER NOT NULL DEFAULT 1000, "
            "timeout INTEGER NOT NULL DEFAULT 1000, "
            "retry_count INTEGER NOT NULL DEFAULT 2, "
            "reconnect_interval INTEGER NOT NULL DEFAULT 3000, "
            "transport TEXT NOT NULL DEFAULT 'TCP', "
            "serial_port TEXT NOT NULL DEFAULT '/dev/ttyS0', "
            "baud_rate INTEGER NOT NULL DEFAULT 9600, "
            "parity TEXT NOT NULL DEFAULT 'N', "
            "data_bits INTEGER NOT NULL DEFAULT 8, "
            "stop_bits INTEGER NOT NULL DEFAULT 1, "
            "enable INTEGER NOT NULL DEFAULT 1"
            ")"
        )
    )
)
{
    QMessageBox::warning(
        this,
        QStringLiteral("Modbus 点表"),
        QStringLiteral(
            "创建 modbus_tasks 表失败：\n%1"
        )
        .arg(
            query.lastError().text()
        )
    );

    db.close();
    return;
}

// 已存在的 TCP 数据库按列增量升级；重复 ALTER 的报错可安全忽略。
const QStringList rtuMigrations = {
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN transport TEXT NOT NULL DEFAULT 'TCP'"),
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN serial_port TEXT NOT NULL DEFAULT '/dev/ttyS0'"),
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN baud_rate INTEGER NOT NULL DEFAULT 9600"),
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN parity TEXT NOT NULL DEFAULT 'N'"),
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN data_bits INTEGER NOT NULL DEFAULT 8"),
    QStringLiteral("ALTER TABLE modbus_tasks ADD COLUMN stop_bits INTEGER NOT NULL DEFAULT 1")
};
for (const QString &migration : rtuMigrations) query.exec(migration);


// 2. 检查 modbus_points 是否已经有 task_id

bool hasTaskIdColumn = false;

if (
    query.exec(
        QStringLiteral(
            "PRAGMA table_info(modbus_points)"
        )
    )
)
{
    while (query.next())
    {
        if (
            query.value(1).toString()
            ==
            QStringLiteral("task_id")
        )
        {
            hasTaskIdColumn = true;
            break;
        }
    }
}


// 3. 老数据库没有 task_id 时自动增加

if (!hasTaskIdColumn)
{
    if (
        !query.exec(
            QStringLiteral(
                "ALTER TABLE modbus_points "
                "ADD COLUMN task_id INTEGER"
            )
        )
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Modbus 点表"),
            QStringLiteral(
                "增加 task_id 字段失败：\n%1"
            )
            .arg(
                query.lastError().text()
            )
        );

        db.close();
        return;
    }
}


// 4. 检查是否已经存在 Modbus 采集任务

int defaultTaskId = -1;

if (
    query.exec(
        QStringLiteral(
            "SELECT id "
            "FROM modbus_tasks "
            "ORDER BY id "
            "LIMIT 1"
        )
    )
    &&
    query.next()
)
{
    defaultTaskId =
        query.value(0).toInt();
}


// 5. 第一次升级时创建默认任务
//
// 先让现在所有已有点都归到 Modbus任务1。

if (defaultTaskId < 0)
{
    QSqlQuery insertTaskQuery(db);

    insertTaskQuery.prepare(
        QStringLiteral(
            "INSERT INTO modbus_tasks "
            "("
            "name, "
            "host, "
            "port, "
            "slave_id, "
            "period, "
            "timeout, "
            "retry_count, "
            "reconnect_interval, "
            "enable"
            ") "
            "VALUES "
            "(?, ?, ?, ?, ?, ?, ?, ?, ?)"
        )
    );

    insertTaskQuery.addBindValue(
        QStringLiteral("Modbus任务1")
    );

    insertTaskQuery.addBindValue(
        QStringLiteral("127.0.0.1")
    );

    insertTaskQuery.addBindValue(502);
    insertTaskQuery.addBindValue(1);

    // 整个任务统一采集周期
    insertTaskQuery.addBindValue(1000);

    insertTaskQuery.addBindValue(1000);
    insertTaskQuery.addBindValue(2);
    insertTaskQuery.addBindValue(3000);
    insertTaskQuery.addBindValue(1);

    if (!insertTaskQuery.exec())
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Modbus 点表"),
            QStringLiteral(
                "创建默认 Modbus 任务失败：\n%1"
            )
            .arg(
                insertTaskQuery
                .lastError()
                .text()
            )
        );

        db.close();
        return;
    }

    defaultTaskId =
        insertTaskQuery
        .lastInsertId()
        .toInt();
}


// 6. 将现有旧点全部归入默认任务
//
// 只处理还没有 task_id 的点。
// 不会覆盖以后已经分配到任务2、任务3的点。

QSqlQuery assignTaskQuery(db);

assignTaskQuery.prepare(
    QStringLiteral(
        "UPDATE modbus_points "
        "SET task_id = ? "
        "WHERE task_id IS NULL "
        "OR task_id <= 0"
    )
);

assignTaskQuery.addBindValue(
    defaultTaskId
);

if (!assignTaskQuery.exec())
{
    QMessageBox::warning(
        this,
        QStringLiteral("Modbus 点表"),
        QStringLiteral(
            "迁移现有 Modbus 点到默认任务失败：\n%1"
        )
        .arg(
            assignTaskQuery
            .lastError()
            .text()
        )
    );

    db.close();
    return;
}

        query.prepare(
    QStringLiteral(
        "SELECT "
        "id, "
        "name, "
        "slave_id, "
        "function, "
        "address, "
        "datatype, "
        "period, "
        "enable, "
        "byte_order, "
        "direction "
        "FROM modbus_points "
        "WHERE task_id = ? "
        "ORDER BY id"
    )
);

query.addBindValue(
    currentModbusTaskId_
);

if (!query.exec())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("Modbus 点表"),
                QStringLiteral(
                    "读取 modbus_points 失败：\n%1"
                )
                .arg(
                    query.lastError().text()
                )
            );

            db.close();
            return;
        }


        // 一行一行读取

        while (query.next())
        {
            const int id =
                query.value(0).toInt();

            const QString name =
                query.value(1).toString();

            const int slaveId =
                query.value(2).toInt();

            const int function =
                query.value(3).toInt();

            const int rawAddress =
                query.value(4).toInt();

            const QString datatype =
                query.value(5).toString();

            const int period =
                query.value(6).toInt();

            QString byteOrder =
                query.value(8).toString();

            if (byteOrder.isEmpty())
            {
                byteOrder =
                    QStringLiteral("ABCD");
            }

            QString direction =
                query.value(9).toString();

            if (
                direction != QStringLiteral("INPUT")
                &&
                direction != QStringLiteral("OUTPUT")
            )
            {
                direction =
                    QStringLiteral("INPUT");
            }

            // 数据库原始地址 -> 界面 Modbus 地址
            //
            // 例如：
            //
            // function=3,address=0
            //       ↓
            //     40001

            QString modbusAddress;

            if (
                function == 1
                ||
                function == 5
                ||
                function == 15
            )
            {
                modbusAddress =
                    QStringLiteral("%1")
                    .arg(
                        rawAddress + 1,
                        5,
                        10,
                        QChar('0')
                    );
            }
            else if (function == 2)
            {
                modbusAddress =
                    QString::number(
                        10001 + rawAddress
                    );
            }
            else if (
                function == 3
                ||
                function == 6
                ||
                function == 16
            )
            {
                modbusAddress =
                    QString::number(
                        40001 + rawAddress
                    );
            }
            else if (function == 4)
            {
                modbusAddress =
                    QString::number(
                        30001 + rawAddress
                    );
            }
            else
            {
                modbusAddress =
                    QString::number(
                        rawAddress
                    );
            }


            // 插入新行

            const int row =
                modbusPointsTable_->rowCount();

            modbusPointsTable_->insertRow(row);


            // 第0列：点名
            //
            // 顺便把数据库里的隐藏信息保存到 UserRole
            // 后面“修改/删除/保存”会使用

            auto *nameItem =
                new QTableWidgetItem(name);

            // 数据库 id
            nameItem->setData(
                Qt::UserRole,
                id
            );

            // Slave ID
            nameItem->setData(
                Qt::UserRole + 1,
                slaveId
            );

            // function
            nameItem->setData(
                Qt::UserRole + 2,
                function
            );

            // 原始 address
            nameItem->setData(
                Qt::UserRole + 3,
                rawAddress
            );

            modbusPointsTable_->setItem(
                row,
                0,
                nameItem
            );


            // 第1列：Modbus 地址

            auto *addressItem = new QTableWidgetItem(modbusAddress);
            addressItem->setToolTip(
                QStringLiteral("双击可自定义 Modbus 地址"));
            modbusPointsTable_->setItem(row, 1, addressItem);


            // 第2列：读写方向

            auto *directionCombo =
                new QComboBox;

            directionCombo->addItems({
                QStringLiteral("INPUT"),
                QStringLiteral("OUTPUT")
            });

            directionCombo->setCurrentText(
                direction
            );

            modbusPointsTable_->setCellWidget(
                row,
                2,
                directionCombo
            );


            // 第3列：数据类型

            auto *datatypeCombo =
                new QComboBox;

            datatypeCombo->addItems({
                QStringLiteral("INT16"),
                QStringLiteral("UINT16"),
                QStringLiteral("INT32"),
                QStringLiteral("UINT32"),
                QStringLiteral("FLOAT"),
                QStringLiteral("DOUBLE")
            });

            datatypeCombo->setCurrentText(
                datatype
            );

            modbusPointsTable_->setCellWidget(
                row,
                3,
                datatypeCombo
            );


            // 第4列：大小端转换

            auto *byteOrderCombo =
                new QComboBox;

            byteOrderCombo->addItems({
                QStringLiteral("ABCD"),
                QStringLiteral("BADC"),
                QStringLiteral("CDAB"),
                QStringLiteral("DCBA")
            });

            byteOrderCombo->setCurrentText(
                byteOrder
            );

            modbusPointsTable_->setCellWidget(
                row,
                4,
                byteOrderCombo
            );


        }


        db.close();
    }


        QSqlDatabase::removeDatabase(
        connectionName
    );
}


// 保存 Modbus 点表
//
// 当前阶段：
// 1. 已有数据库点 -> UPDATE
// 2. 新建点       -> INSERT
//
// 暂时不处理已经从 UI 删除的点

void saveModbusPointsTable()
{
    if (!modbusPointsTable_)
        return;

    const QString connectionName =
        QStringLiteral("modbus_points_save_%1")
        .arg(
            reinterpret_cast<quintptr>(this)
        );

    bool success = true;
    QString errorMessage;


    // 使用独立作用域，确保 removeDatabase 前释放 db/query

    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );

        db.setDatabaseName(
            modbusConfigDatabase_
        );


        // 打开数据库

        if (!db.open())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("保存 Modbus 点表"),
                QStringLiteral(
                    "无法打开 Modbus 配置数据库：\n%1\n\n%2"
                )
                .arg(
                    modbusConfigDatabase_,
                    db.lastError().text()
                )
            );

            return;
        }


        // 开启事务
        //
        // 如果中间某个点保存失败，就全部撤销

        if (!db.transaction())
        {
            QMessageBox::warning(
                this,
                QStringLiteral("保存 Modbus 点表"),
                QStringLiteral(
                    "无法启动数据库事务：\n%1"
                )
                .arg(
                    db.lastError().text()
                )
            );

            db.close();
            return;
        }

// 获取当前 Modbus 任务的统一采集周期
//
// 当前 Collector 仍然读取 modbus_points.period，
// 所以过渡阶段保存点时，将任务周期同步写入旧 period 字段。
// 后续 Collector 改为直接读取 modbus_tasks.period 后，
// 这里即可删除。

int taskPeriod = 1000;

{
    QSqlQuery taskQuery(db);

    taskQuery.prepare(
        QStringLiteral(
            "SELECT period "
            "FROM modbus_tasks "
            "WHERE id = ?"
        )
    );

    taskQuery.addBindValue(
        currentModbusTaskId_
    );

    if (
        taskQuery.exec()
        &&
        taskQuery.next()
    )
    {
        taskPeriod =
            taskQuery.value(0).toInt();
    }
}

if (taskPeriod <= 0)
{
    taskPeriod = 1000;
}

        // 一行一行读取 Qt 点表

        for (
            int row = 0;
            row < modbusPointsTable_->rowCount();
            ++row
        )
        {
            // 获取普通单元格

            QTableWidgetItem *nameItem =
                modbusPointsTable_->item(
                    row,
                    0
                );

            QTableWidgetItem *addressItem =
                modbusPointsTable_->item(
                    row,
                    1
                );

            // 获取三个下拉框

            auto *directionCombo =
                qobject_cast<QComboBox *>(
                    modbusPointsTable_->cellWidget(
                        row,
                        2
                    )
                );

            auto *datatypeCombo =
                qobject_cast<QComboBox *>(
                    modbusPointsTable_->cellWidget(
                        row,
                        3
                    )
                );

            auto *byteOrderCombo =
                qobject_cast<QComboBox *>(
                    modbusPointsTable_->cellWidget(
                        row,
                        4
                    )
                );


            // 检查这一行是否完整

            if (
                !nameItem
                ||
                !addressItem
                ||
                !directionCombo
                ||
                !datatypeCombo
                ||
                !byteOrderCombo
            )
            {
                success = false;

                errorMessage =
                    QStringLiteral(
                        "第 %1 行的数据不完整。"
                    )
                    .arg(row + 1);

                break;
            }


            // 点名

            const QString name =
                nameItem->text().trimmed();

            if (name.isEmpty())
            {
                success = false;

                errorMessage =
                    QStringLiteral(
                        "第 %1 行的点名不能为空。"
                    )
                    .arg(row + 1);

                break;
            }


            // 界面 Modbus 地址

            bool addressOk = false;

            const int modbusAddress =
                addressItem
                ->text()
                .trimmed()
                .toInt(
                    &addressOk
                );

            if (!addressOk)
            {
                success = false;

                errorMessage =
                    QStringLiteral(
                        "点“%1”的 Modbus 地址无效。"
                    )
                    .arg(name);

                break;
            }


            // 界面地址 -> function + 数据库原始 address
            //
            // 00001~09999 -> FC01
            // 10001~19999 -> FC02
            // 30001~39999 -> FC04
            // 40001~49999 -> FC03
            //
            // 例如：
            // 40001 -> function=3,address=0
            // 40010 -> function=3,address=9

            int function = 0;
            int rawAddress = -1;

            if (
                modbusAddress >= 1
                &&
                modbusAddress <= 9999
            )
            {
                function = 1;
                rawAddress =
                    modbusAddress - 1;
            }
            else if (
                modbusAddress >= 10001
                &&
                modbusAddress <= 19999
            )
            {
                function = 2;
                rawAddress =
                    modbusAddress - 10001;
            }
            else if (
                modbusAddress >= 30001
                &&
                modbusAddress <= 39999
            )
            {
                function = 4;
                rawAddress =
                    modbusAddress - 30001;
            }
            else if (
                modbusAddress >= 40001
                &&
                modbusAddress <= 49999
            )
            {
                function = 3;
                rawAddress =
                    modbusAddress - 40001;
            }
            else
            {
                success = false;

                errorMessage =
                    QStringLiteral(
                        "点“%1”的 Modbus 地址 %2 "
                        "不在当前支持的地址范围内。"
                    )
                    .arg(name)
                    .arg(modbusAddress);

                break;
            }


            // 下拉框配置

            const QString direction =
                directionCombo->currentText();

            const QString datatype =
                datatypeCombo->currentText();

            const QString byteOrder =
                byteOrderCombo->currentText();

            if (
                direction == QStringLiteral("OUTPUT")
                &&
                (modbusAddress < 40001 || modbusAddress > 49999)
            )
            {
                success = false;
                errorMessage = QStringLiteral(
                    "点“%1”是 OUTPUT，必须配置在可写的"
                    "保持寄存器地址 40001~49999。")
                    .arg(name);
                break;
            }


            // 点表不再提供单点启停，所有保存的点默认启用。
            const int enabled = 1;


            // Slave ID
            //
            // 已有点：
            // loadModbusPointsTable() 已经放在 UserRole+1
            //
            // 新建点：
            // 我们之前默认放的是 1

            int slaveId =
                nameItem
                ->data(
                    Qt::UserRole + 1
                )
                .toInt();

            if (
                slaveId < 1
                ||
                slaveId > 247
            )
            {
                slaveId = 1;
            }


            // 获取数据库 ID
            //
            // 已有点：真实 id
            // 新建点：-1

            const int id =
                nameItem
                ->data(
                    Qt::UserRole
                )
                .toInt();


            // 已有点 -> UPDATE

            if (id >= 0)
            {
                QSqlQuery updateQuery(db);

                updateQuery.prepare(
                    QStringLiteral(
                        "UPDATE modbus_points "
                        "SET "
                        "name = ?, "
                        "slave_id = ?, "
                        "function = ?, "
                        "address = ?, "
                        "datatype = ?, "
                        "period = ?, "
                        "enable = ?, "
                        "byte_order = ?, "
                        "direction = ? "
                        "WHERE id = ? "
                        "AND task_id = ?"
                    )
                );

                updateQuery.addBindValue(name);
                updateQuery.addBindValue(slaveId);
                updateQuery.addBindValue(function);
                updateQuery.addBindValue(rawAddress);
                updateQuery.addBindValue(datatype);
                updateQuery.addBindValue(taskPeriod);
                updateQuery.addBindValue(enabled);
                updateQuery.addBindValue(byteOrder);
                updateQuery.addBindValue(direction);
                updateQuery.addBindValue(id);
                updateQuery.addBindValue(currentModbusTaskId_);

                if (!updateQuery.exec())
                {
                    success = false;

                    errorMessage =
                        QStringLiteral(
                            "修改点“%1”失败：\n%2"
                        )
                        .arg(
                            name,
                            updateQuery
                            .lastError()
                            .text()
                        );

                    break;
                }
            }


            // 新建点 -> INSERT

            else
            {
                QSqlQuery insertQuery(db);

                insertQuery.prepare(
                    QStringLiteral(
                        "INSERT INTO modbus_points "
                        "("
                        "name, "
                        "slave_id, "
                        "function, "
                        "address, "
                        "datatype, "
                        "period, "
                        "enable, "
                        "byte_order, "
                        "direction, "
                        "task_id"
                        ") "
                        "VALUES "
                        "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                    )
                );

                insertQuery.addBindValue(name);
                insertQuery.addBindValue(slaveId);
                insertQuery.addBindValue(function);
                insertQuery.addBindValue(rawAddress);
                insertQuery.addBindValue(datatype);
                insertQuery.addBindValue(taskPeriod);
                insertQuery.addBindValue(enabled);
                insertQuery.addBindValue(byteOrder);
                insertQuery.addBindValue(direction);
                insertQuery.addBindValue(currentModbusTaskId_);

                if (!insertQuery.exec())
                {
                    success = false;

                    errorMessage =
                        QStringLiteral(
                            "新建点“%1”失败：\n%2"
                        )
                        .arg(
                            name,
                            insertQuery
                            .lastError()
                            .text()
                        );

                    break;
                }


                // SQLite 自动生成了新的 id
                // 把它写回 UI，防止下一次保存再次 INSERT

                const int newId =
                    insertQuery
                    .lastInsertId()
                    .toInt();

                nameItem->setData(
                    Qt::UserRole,
                    newId
                );

                nameItem->setData(
                    Qt::UserRole + 2,
                    function
                );

                nameItem->setData(
                    Qt::UserRole + 3,
                    rawAddress
                );
            }
        }


        // 所有点都成功 -> COMMIT
        // 任意一点失败 -> ROLLBACK

        if (success)
        {
            if (!db.commit())
            {
                success = false;

                errorMessage =
                    QStringLiteral(
                        "提交数据库事务失败：\n%1"
                    )
                    .arg(
                        db.lastError().text()
                    );

                db.rollback();
            }
        }
        else
        {
            db.rollback();
        }


        db.close();
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );


    // 显示保存结果

    if (!success)
    {
        QMessageBox::warning(
            this,
            QStringLiteral("保存 Modbus 点表"),
            errorMessage
        );

        return;
    }


    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 点表保存成功。"
        ),
        3000
    );
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

        // Modbus 工程：创建 Modbus TCP 通信总览行

        if (currentCommunicationType_ == QStringLiteral("modbus"))
        {
            overview_->insertRow(0);

            const QStringList values = {
                QStringLiteral("○ 待检测"),
                QStringLiteral("Modbus 通信"),
                QStringLiteral("Modbus TCP / 主站"),
                QStringLiteral("127.0.0.1:502"),
                QStringLiteral("0 / 0"),
                QStringLiteral("--"),
                QStringLiteral("待启动")
            };

            for (int column = 0; column < values.size(); ++column)
            {
                overview_->setItem(
                    0,
                    column,
                    new QTableWidgetItem(values[column])
                );
            }

            overview_->item(0, 0)->setForeground(
                QColor("#808000")
            );
        }

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
        bar->addSpacing(12);
        bar->addWidget(new QLabel(QStringLiteral("来源：")));
        frameSourceFilter_ = new QComboBox;
        frameSourceFilter_->addItems({QStringLiteral("全部"), QStringLiteral("Modbus"), QStringLiteral("Python")});
        bar->addWidget(frameSourceFilter_);
        bar->addWidget(new QLabel(QStringLiteral("方向：")));
        frameDirectionFilter_ = new QComboBox;
        frameDirectionFilter_->addItems({QStringLiteral("全部"), QStringLiteral("TX"), QStringLiteral("RX")});
        bar->addWidget(frameDirectionFilter_);
        bar->addWidget(new QLabel(QStringLiteral("功能码：")));
        frameFunctionFilter_ = new QComboBox;
        frameFunctionFilter_->addItems({QStringLiteral("全部"), QStringLiteral("03 读保持寄存器"),
                                        QStringLiteral("04 读输入寄存器"), QStringLiteral("06 写单寄存器"),
                                        QStringLiteral("16 写多寄存器"), QStringLiteral("仅写操作（06/16）")});
        bar->addWidget(frameFunctionFilter_);
        auto *source = new QLabel(
            QStringLiteral("监视来源：Python Runner / Modbus TCP")
        );
        source->setStyleSheet(QStringLiteral("color:#000080;"));
        bar->addWidget(source);
        bar->addStretch();
        rxRate_ = sunkenLabel(QStringLiteral("实时采集"), 90);
        bar->addWidget(rxRate_);
        layout->addLayout(bar);

        auto *split = new QSplitter(Qt::Vertical);
        frames_ = new QTableWidget(0, 7);
        frames_->setHorizontalHeaderLabels({
            QStringLiteral("时间"),
            QStringLiteral("来源"),
            QStringLiteral("任务 / 点名"),
            QStringLiteral("功能"),
            QStringLiteral("摘要"),
            QStringLiteral("长度"),
            QStringLiteral("方向")
        });
        frames_->setAlternatingRowColors(true);
        frames_->setSelectionBehavior(QAbstractItemView::SelectRows);
        frames_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        frames_->verticalHeader()->setVisible(false);
        frames_->setShowGrid(false);
        frames_->setWordWrap(false);
        frames_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        frames_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        frames_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        frames_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        frames_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        frames_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        split->addWidget(frames_);

        auto *detailPanel = new QWidget;
        auto *detailLayout = new QVBoxLayout(detailPanel);
        detailLayout->setContentsMargins(0, 0, 0, 0);
        detailLayout->addWidget(new QLabel(QStringLiteral("完整内容（单击上方摘要查看）：")));
        frameDetail_ = new QPlainTextEdit;
        frameDetail_->setReadOnly(true);
        frameDetail_->setFont(QFont(QStringLiteral("Monaco"), 11));
        frameDetail_->setPlaceholderText(
            QStringLiteral("选择一条记录后在这里显示完整内容。")
        );
        detailLayout->addWidget(frameDetail_);
        split->addWidget(detailPanel);
        split->setSizes({300, 180});
        layout->addWidget(split);
        connect(clear, &QPushButton::clicked, frames_, [this] {
            frames_->setRowCount(0);

            lastModbusFrameKeys_.clear();

            if (frameDetail_)
                frameDetail_->clear();
        });
        connect(pause, &QPushButton::clicked, pause, [this, pause] {
            framePaused_ = !framePaused_;
            pause->setText(framePaused_ ? QStringLiteral("▶ 继续") : QStringLiteral("Ⅱ 暂停"));
            if (rxRate_) rxRate_->setText(framePaused_ ? QStringLiteral("已暂停") : QStringLiteral("实时采集"));
        });
        connect(frames_, &QTableWidget::itemSelectionChanged, frames_, [this] {
            if (!frameDetail_) return;
            const int row = frames_->currentRow();
            QTableWidgetItem *summary = row >= 0 ? frames_->item(row, 4) : nullptr;
            frameDetail_->setPlainText(summary ? summary->data(Qt::UserRole).toString() : QString());
        });
        connect(frameSourceFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyFrameFilters(); });
        connect(frameDirectionFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyFrameFilters(); });
        connect(frameFunctionFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyFrameFilters(); });
        return page;
    }

    QWidget *createPropertyPanel()
    {
        auto *scroll =
            new QScrollArea;

        scroll->setWidgetResizable(
            true
        );


        auto *panel =
            new QWidget;

        auto *layout =
            new QVBoxLayout(
                panel
            );


        // 基本

        auto *basicGroup =
            new QGroupBox(
                QStringLiteral("基本")
            );

        auto *basicForm =
            new QFormLayout(
                basicGroup
            );

        basicForm->setFieldGrowthPolicy(
            QFormLayout::AllNonFixedFieldsGrow
        );


        modbusTaskNameEdit_ =
            new QLineEdit;

        modbusTaskProtocolEdit_ = new QComboBox;
        modbusTaskProtocolEdit_->addItems({QStringLiteral("Modbus TCP"), QStringLiteral("Modbus RTU")});

        modbusTaskRoleEdit_ =
            new QLineEdit(
                QStringLiteral("主站")
            );


        // 当前实现为主站；传输模式可以按任务选择。
        modbusTaskRoleEdit_->setReadOnly(
            true
        );


        basicForm->addRow(
            QStringLiteral("任务名称"),
            modbusTaskNameEdit_
        );

        basicForm->addRow(
            QStringLiteral("协议"),
            modbusTaskProtocolEdit_
        );

        basicForm->addRow(
            QStringLiteral("角色"),
            modbusTaskRoleEdit_
        );


        layout->addWidget(
            basicGroup
        );


        // 端点

        auto *endpointGroup =
            new QGroupBox(
                QStringLiteral("端点")
            );

        auto *endpointForm =
            new QFormLayout(
                endpointGroup
            );

        endpointForm->setFieldGrowthPolicy(
            QFormLayout::AllNonFixedFieldsGrow
        );


        modbusTaskHostEdit_ =
            new QLineEdit;

        modbusTaskPortEdit_ =
            new QLineEdit;

        modbusTaskSlaveEdit_ =
            new QLineEdit;

        modbusTaskSerialPortEdit_ = new QLineEdit(QStringLiteral("/dev/ttyS0"));
        modbusTaskBaudEdit_ = new QComboBox;
        modbusTaskBaudEdit_->setEditable(true);
        modbusTaskBaudEdit_->addItems({QStringLiteral("1200"), QStringLiteral("2400"), QStringLiteral("4800"),
                                       QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
                                       QStringLiteral("57600"), QStringLiteral("115200")});
        modbusTaskBaudEdit_->setCurrentText(QStringLiteral("9600"));
        modbusTaskParityEdit_ = new QComboBox;
        modbusTaskParityEdit_->addItems({QStringLiteral("N（无校验）"), QStringLiteral("E（偶校验）"), QStringLiteral("O（奇校验）")});
        modbusTaskDataBitsEdit_ = new QComboBox;
        modbusTaskDataBitsEdit_->addItems({QStringLiteral("8"), QStringLiteral("7")});
        modbusTaskStopBitsEdit_ = new QComboBox;
        modbusTaskStopBitsEdit_->addItems({QStringLiteral("1"), QStringLiteral("2")});

        modbusTaskHostLabel_ = new QLabel(QStringLiteral("IP 地址"));
        modbusTaskPortLabel_ = new QLabel(QStringLiteral("端口"));
        modbusTaskSerialPortLabel_ = new QLabel(QStringLiteral("串口设备"));
        modbusTaskBaudLabel_ = new QLabel(QStringLiteral("波特率"));
        modbusTaskParityLabel_ = new QLabel(QStringLiteral("校验位"));
        modbusTaskDataBitsLabel_ = new QLabel(QStringLiteral("数据位"));
        modbusTaskStopBitsLabel_ = new QLabel(QStringLiteral("停止位"));

        endpointForm->addRow(modbusTaskHostLabel_, modbusTaskHostEdit_);
        endpointForm->addRow(modbusTaskPortLabel_, modbusTaskPortEdit_);
        endpointForm->addRow(modbusTaskSerialPortLabel_, modbusTaskSerialPortEdit_);
        endpointForm->addRow(modbusTaskBaudLabel_, modbusTaskBaudEdit_);
        endpointForm->addRow(modbusTaskParityLabel_, modbusTaskParityEdit_);
        endpointForm->addRow(modbusTaskDataBitsLabel_, modbusTaskDataBitsEdit_);
        endpointForm->addRow(modbusTaskStopBitsLabel_, modbusTaskStopBitsEdit_);

        endpointForm->addRow(
            QStringLiteral("Unit ID"),
            modbusTaskSlaveEdit_
        );

        auto updateTransportFields = [this] {
            const bool rtu = modbusTaskProtocolEdit_ && modbusTaskProtocolEdit_->currentText().contains(QStringLiteral("RTU"));
            for (QWidget *widget : QList<QWidget *>{modbusTaskHostLabel_, modbusTaskHostEdit_,
                                                   modbusTaskPortLabel_, modbusTaskPortEdit_})
                if (widget) widget->setVisible(!rtu);
            for (QWidget *widget : QList<QWidget *>{modbusTaskSerialPortLabel_, modbusTaskSerialPortEdit_,
                                                   modbusTaskBaudLabel_, modbusTaskBaudEdit_,
                                                   modbusTaskParityLabel_, modbusTaskParityEdit_,
                                                   modbusTaskDataBitsLabel_, modbusTaskDataBitsEdit_,
                                                   modbusTaskStopBitsLabel_, modbusTaskStopBitsEdit_})
                if (widget) widget->setVisible(rtu);
        };
        connect(modbusTaskProtocolEdit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [updateTransportFields](int) { updateTransportFields(); });
        updateTransportFields();


        layout->addWidget(
            endpointGroup
        );


        // 时序

        auto *timingGroup =
            new QGroupBox(
                QStringLiteral("时序")
            );

        auto *timingForm =
            new QFormLayout(
                timingGroup
            );

        timingForm->setFieldGrowthPolicy(
            QFormLayout::AllNonFixedFieldsGrow
        );


        modbusTaskPeriodEdit_ =
            new QLineEdit;

        modbusTaskTimeoutEdit_ =
            new QLineEdit;

        modbusTaskRetryEdit_ =
            new QLineEdit;

        modbusTaskReconnectEdit_ =
            new QLineEdit;


        timingForm->addRow(
            QStringLiteral("采集周期"),
            modbusTaskPeriodEdit_
        );

        timingForm->addRow(
            QStringLiteral("响应超时"),
            modbusTaskTimeoutEdit_
        );

        timingForm->addRow(
            QStringLiteral("重试次数"),
            modbusTaskRetryEdit_
        );

        timingForm->addRow(
            QStringLiteral("重连间隔"),
            modbusTaskReconnectEdit_
        );


        layout->addWidget(
            timingGroup
        );


        // 保存按钮

        saveModbusTaskButton_ =
            new QPushButton(
                QStringLiteral("保存任务配置")
            );

        layout->addWidget(
            saveModbusTaskButton_
        );


        connect(
            saveModbusTaskButton_,
            &QPushButton::clicked,
            this,
            [this]
            {
                saveCurrentModbusTask();
            }
        );


        layout->addStretch();

        scroll->setWidget(
            panel
        );

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
    auto *timer =
        new QTimer(this);


    connect(
        timer,
        &QTimer::timeout,
        this,
        [this]
        {
            clock_->setText(
                QDateTime::currentDateTime()
                    .toString(
                        QStringLiteral(
                            "yyyy-MM-dd  HH:mm:ss"
                        )
                    )
            );


            // 在线数据

            if (onlineTable_)
            {
                if (
                    currentCommunicationType_
                    ==
                    QStringLiteral("modbus")
                )
                {
                    // Modbus 数据由 modbusTimer_ 负责刷新
                }
                else if (
                    currentCommunicationType_
                    ==
                    QStringLiteral("python")
                )
                {
                    pollManager();
                }
            }


            // Python 调试数据

            if (
                currentCommunicationType_
                ==
                QStringLiteral("python")
            )
            {
                if (overview_ || frames_)
                {
                    pollPythonDebug();
                }

                pollRuntimeLog();
            }
        }
    );


    timer->start(1000);


    clock_->setText(
        QDateTime::currentDateTime()
            .toString(
                QStringLiteral(
                    "yyyy-MM-dd  HH:mm:ss"
                )
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
    if (
        !projectRootItem_
        ||
        !pythonTasksItem_
        ||
        !modbusTasksItem_
    )
    {
        return;
    }

    projectRootItem_->setText(
        0,
        currentProjectName_
    );


    // 清理 Python 任务
    while (pythonTasksItem_->childCount())
    {
        delete pythonTasksItem_->takeChild(0);
    }


    // 清理 Modbus 任务
    while (modbusTasksItem_->childCount())
    {
        delete modbusTasksItem_->takeChild(0);
    }

    modbusPointsItem_ = nullptr;
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
// 从 ModbusCollector 数据库加载 Modbus 采集任务

const QString modbusConnectionName =
    QStringLiteral("modbus_tree_%1")
    .arg(
        reinterpret_cast<quintptr>(this)
    );

{
    QSqlDatabase modbusDb =
        QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"),
            modbusConnectionName
        );

    modbusDb.setDatabaseName(
        modbusConfigDatabase_
    );


    if (modbusDb.open())
    {
        QSqlQuery modbusQuery(
            modbusDb
        );

        if (
            modbusQuery.exec(
                QStringLiteral(
                    "SELECT "
                    "t.id, "
                    "t.name, "
                    "t.enable, "
                    "("
                    "SELECT COUNT(*) "
                    "FROM modbus_points p "
                    "WHERE p.task_id = t.id"
                    ") "
                    "FROM modbus_tasks t "
                    "ORDER BY t.id"
                )
            )
        )
        {
            while (
                modbusQuery.next()
            )
            {
                const qlonglong taskId =
                    modbusQuery
                    .value(0)
                    .toLongLong();

                const QString taskName =
                    modbusQuery
                    .value(1)
                    .toString();

                const bool enabled =
                    modbusQuery
                    .value(2)
                    .toBool();

                const int modbusPointCount =
                    modbusQuery
                    .value(3)
                    .toInt();


                // Modbus 任务节点

                auto *taskItem =
                    new QTreeWidgetItem(
                        modbusTasksItem_
                    );

                taskItem->setText(
                    0,
                    taskName
                    +
                    (
                        enabled
                        ?
                        QStringLiteral(" [启用]")
                        :
                        QStringLiteral(" [停用]")
                    )
                );

                taskItem->setIcon(
                    0,
                    pixelIcon(
                        QColor("#800080"),
                        1
                    )
                );

                taskItem->setData(
                    0,
                    Qt::UserRole,
                    QStringLiteral(
                        "modbus_task"
                    )
                );

                // 保存数据库中的 task_id
                taskItem->setData(
                    0,
                    Qt::UserRole + 1,
                    taskId
                );


                // 当前任务下面的点表

                auto *pointsItem =
                    new QTreeWidgetItem(
                        taskItem
                    );

                pointsItem->setText(
                    0,
                    QStringLiteral(
                        "点表 (%1)"
                    )
                    .arg(
                        modbusPointCount
                    )
                );

                pointsItem->setIcon(
                    0,
                    pixelIcon(
                        QColor("#008080"),
                        1
                    )
                );

                pointsItem->setData(
                    0,
                    Qt::UserRole,
                    QStringLiteral(
                        "modbus_points"
                    )
                );

                // 点表节点同样保存所属 task_id
                pointsItem->setData(
                    0,
                    Qt::UserRole + 1,
                    taskId
                );


                // 为兼容当前已有成员变量，
                // 暂时让它指向第一个 Modbus 点表。
                if (!modbusPointsItem_)
                {
                    modbusPointsItem_ =
                        pointsItem;
                }


                taskItem->setExpanded(
                    true
                );
            }
        }

        modbusDb.close();
    }
}

QSqlDatabase::removeDatabase(
    modbusConnectionName
);
        if (pointDirectoryItem_)
            pointDirectoryItem_->setText(0, QStringLiteral("点目录 (%1)").arg(pointCount));
        projectRootItem_->setExpanded(true);
pythonTasksItem_->setExpanded(true);

if (modbusCommunicationItem_)
{
    modbusCommunicationItem_->setExpanded(
        true
    );
}

if (modbusTasksItem_)
{
    modbusTasksItem_->setExpanded(
        true
    );
}
        if (projectVersionLabel_)
            projectVersionLabel_->setText(QStringLiteral("工程点数: %1").arg(pointCount));
                if (overview_) {
            if (taskSummary_)
                taskSummary_->setText(
                    QStringLiteral("0 / %1").arg(taskRows.size())
                );

            if (pointSummary_)
                pointSummary_->setText(
                    QStringLiteral("%1 点").arg(pointCount)
                );
        }
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

    dialog.setWindowTitle(
        QStringLiteral("新建通信工程")
    );


    auto *layout =
        new QVBoxLayout(&dialog);


    auto *form =
        new QFormLayout;


    auto *name =
        new QLineEdit(
            QStringLiteral("新建通信工程")
        );


    auto *parent =
        new QLineEdit(
            QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation
            )
        );


    auto *choose =
        new QPushButton(
            QStringLiteral("浏览...")
        );


    auto *pathRow =
        new QHBoxLayout;


    pathRow->addWidget(
        parent,
        1
    );

    pathRow->addWidget(
        choose
    );


    form->addRow(
        QStringLiteral("工程名称:"),
        name
    );


    form->addRow(
        QStringLiteral("保存位置:"),
        pathRow
    );


    layout->addLayout(form);


    auto *buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Ok |
            QDialogButtonBox::Cancel
        );


    buttons
        ->button(
            QDialogButtonBox::Ok
        )
        ->setText(
            QStringLiteral("创建工程")
        );


    layout->addWidget(buttons);


    // 选择目录

    connect(
        choose,
        &QPushButton::clicked,
        &dialog,
        [&]()
        {
            const QString selected =
                QFileDialog::getExistingDirectory(
                    &dialog,
                    QStringLiteral("选择工程保存位置"),
                    parent->text()
                );


            if (!selected.isEmpty())
            {
                parent->setText(selected);
            }
        }
    );


    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject
    );


    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        &QDialog::accept
    );


    if (
        dialog.exec()
        !=
        QDialog::Accepted
    )
    {
        return;
    }


    // 工程名称

    const QString projectName =
        name->text().trimmed();


    QString folderName =
        projectName;


    folderName.replace(
        QRegularExpression(
            QStringLiteral(
                "[\\/:*?\"<>|]"
            )
        ),
        QStringLiteral("_")
    );


    if (
        projectName.isEmpty()
        ||
        folderName.isEmpty()
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("名称无效"),
            QStringLiteral(
                "请输入有效的工程名称。"
            )
        );

        return;
    }


    // 创建工程目录

    QDir parentDirectory(
        parent->text()
    );


    const QString directoryPath =
        parentDirectory.filePath(
            folderName
        );


    if (
        QFileInfo::exists(
            directoryPath
        )
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("目录已存在"),
            directoryPath
        );

        return;
    }


    if (
        !QDir().mkpath(
            QDir(directoryPath)
                .filePath(
                    QStringLiteral("scripts")
                )
        )
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("创建失败"),
            QStringLiteral(
                "无法创建工程目录。"
            )
        );

        return;
    }


    // 创建 SQLite 配置数据库
    //
    // Python 通信使用

    const QString databasePath =
        QDir(directoryPath)
            .filePath(
                QStringLiteral(
                    "config.sqlite"
                )
            );


    const QString generator =
        QDir(workspaceRoot())
            .filePath(
                QStringLiteral(
                    "python-communication-demo/"
                    "tools/create_demo_db.py"
                )
            );


    QProcess generatorProcess;


    generatorProcess.start(
        QStringLiteral("python3"),
        {
            generator,
            databasePath
        }
    );


    if (
        !generatorProcess.waitForFinished(10000)
        ||
        generatorProcess.exitCode()
        != 0
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("创建失败"),
            QStringLiteral(
                "无法初始化 SQLite 配置库。"
            )
        );

        return;
    }


    // 初始化空工程

    const QString connectionName =
        QStringLiteral(
            "create_project_%1"
        )
        .arg(
            QCoreApplication::applicationPid()
        );


    bool databaseReady = false;


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                connectionName
            );


        db.setDatabaseName(
            databasePath
        );


        if (
            db.open()
            &&
            db.transaction()
        )
        {
            QSqlQuery query(db);


            const QString projectHash =
                QUuid::createUuid()
                    .toString(
                        QUuid::WithoutBraces
                    );


            databaseReady =
                query.exec(
                    QStringLiteral(
                        "DELETE FROM python_binding"
                    )
                )
                &&
                query.exec(
                    QStringLiteral(
                        "DELETE FROM python_task"
                    )
                )
                &&
                query.exec(
                    QStringLiteral(
                        "DELETE FROM point"
                    )
                )
                &&
                query.exec(
                    QStringLiteral(
                        "UPDATE config_version "
                        "SET content_hash='%1' "
                        "WHERE status='PUBLISHED'"
                    )
                    .arg(projectHash)
                )
                &&
                db.commit();


            if (!databaseReady)
            {
                db.rollback();
            }


            db.close();
        }
    }


    QSqlDatabase::removeDatabase(
        connectionName
    );


    if (!databaseReady)
    {
        QMessageBox::critical(
            this,
            QStringLiteral("创建失败"),
            QStringLiteral(
                "无法初始化空工程配置。"
            )
        );

        return;
    }


    // 创建 project.json

    QSaveFile metadata(
        QDir(directoryPath)
            .filePath(
                QStringLiteral(
                    "project.json"
                )
            )
    );


    if (
        !metadata.open(
            QIODevice::WriteOnly |
            QIODevice::Text
        )
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("创建失败"),
            metadata.errorString()
        );

        return;
    }


    metadata.write(
        QJsonDocument(
            QJsonObject{
                {
                    QStringLiteral(
                        "formatVersion"
                    ),
                    1
                },
                {
                    QStringLiteral(
                        "name"
                    ),
                    projectName
                },
                {
                    QStringLiteral(
                        "database"
                    ),
                    QStringLiteral(
                        "config.sqlite"
                    )
                },
                {
                    QStringLiteral(
                        "scripts"
                    ),
                    QStringLiteral(
                        "scripts"
                    )
                }
            }
        )
        .toJson(
            QJsonDocument::Indented
        )
    );


    if (!metadata.commit())
    {
        QMessageBox::critical(
            this,
            QStringLiteral("创建失败"),
            metadata.errorString()
        );

        return;
    }


    // 加载刚创建的工程
    //
    // 注意：
    // 这里只加载工程
    //
    // 不启动 Python 检测
    // 不启动 Modbus 检测

    loadProjectFile(
        QDir(directoryPath)
            .filePath(
                QStringLiteral(
                    "project.json"
                )
            )
    );


    // 默认不选择任何检测状态

    currentCommunicationType_.clear();


    statusBar()->showMessage(
        QStringLiteral(
            "工程创建成功。请选择采集设备中的通信通道，然后点击“启动检测”。"
        ),
        5000
    );
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
    QString error;
    QJsonObject ping =
        modbusServerClient_.request(
            1,
            modbusRequestId_,
            &error
        );

    if (ping.isEmpty())
    {
        const QString collectorDir = QStringLiteral("/home/xu/桌面/ModbusCollector");
        const QString executable = QDir(collectorDir).filePath(QStringLiteral("build/ModbusCollector"));
        if (!QFileInfo(executable).isExecutable()) {
            QMessageBox::warning(this, QStringLiteral("无法启动 Modbus 采集服务"),
                QStringLiteral("没有找到可执行的 ModbusCollector：\n%1\n\n请先编译 Collector。")
                    .arg(executable));
            return;
        }

        if (modbusCollectorProcess_) modbusCollectorProcess_->deleteLater();
        modbusCollectorProcess_ = new QProcess(this);
        modbusCollectorProcess_->setWorkingDirectory(collectorDir);
        modbusCollectorProcess_->setProcessChannelMode(QProcess::ForwardedChannels);
        connect(modbusCollectorProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code) {
                    if (serviceStatus_) {
                        serviceStatus_->setText(QStringLiteral("○ Modbus 采集服务已退出"));
                        serviceStatus_->setStyleSheet(QStringLiteral("color:#c00000;font-weight:bold;"));
                    }
                    statusBar()->showMessage(QStringLiteral("ModbusCollector 已退出，代码 %1").arg(code), 5000);
                });
        modbusCollectorProcess_->start(executable, QStringList());
        if (!modbusCollectorProcess_->waitForStarted(2000)) {
            QMessageBox::warning(this, QStringLiteral("无法启动 Modbus 采集服务"),
                                 modbusCollectorProcess_->errorString());
            return;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000 && ping.isEmpty()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(100);
            error.clear();
            ping = modbusServerClient_.request(1, modbusRequestId_, &error);
        }
        if (ping.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Modbus 采集服务启动失败"),
                QStringLiteral("ModbusCollector 已启动，但本机采集接口尚未就绪。\n\n错误信息：%1")
                    .arg(error));
            return;
        }
    }


    // 2. 更新工程状态

    if (overview_)
{
    for (
        int row = 0;
        row < overview_->rowCount();
        ++row
    )
    {
        QTableWidgetItem *channel =
            overview_->item(
                row,
                1
            );


        if (
            !channel
            ||
            channel->text()
            !=
            QStringLiteral("Modbus 通信")
        )
        {
            continue;
        }


        QTableWidgetItem *state =
            overview_->item(
                row,
                0
            );


        if (state)
        {
            state->setText(
                QStringLiteral("● 运行")
            );

            state->setForeground(
                QColor("#008000")
            );
        }


        if (
            overview_->item(
                row,
                3
            )
        )
        {
            overview_->item(
                row,
                3
            )
            ->setText(
                QStringLiteral(
                    "127.0.0.1:502"
                )
            );
        }


        if (
            overview_->item(
                row,
                6
            )
        )
        {
            overview_->item(
                row,
                6
            )
            ->setText(
                QStringLiteral(
                    "运行正常"
                )
            );
        }


        break;
    }
}


    // 3. 立即读取一次采集服务数据

    pollModbusServer();


    // 4. 启动本机数据定时刷新

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
                pollModbusServer();
            }
        );
    }


    modbusTimer_->start(500);


    // 5. 更新界面状态

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
            "Modbus 检测已启动（设备通道按任务使用 TCP 或 RTU）"
        ),
        3000
    );
}

    void startManager()
{
    // 必须先选择通信通道

    if (
        currentCommunicationType_.isEmpty()
    )
    {
        QMessageBox::information(
            this,
            QStringLiteral("请选择通信通道"),
            QStringLiteral(
                "请先在左侧工程树中选择：\n\n"
                "采集设备 → Python 通信\n\n"
                "或\n\n"
                "采集设备 → Modbus 通信\n\n"
                "然后再点击“启动检测”。"
            )
        );

        return;
    }


    // Modbus

    if (
        currentCommunicationType_
        ==
        QStringLiteral("modbus")
    )
    {
        startModbusDetection();

        return;
    }


    // Python

    if (
        currentCommunicationType_
        !=
        QStringLiteral("python")
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("通信类型错误"),
            QStringLiteral("未知的通信类型。")
        );

        return;
    }


    // 以下保持原来的 Python system-managerd 逻辑

    QString error;

    const QJsonObject existing =
        queryManager(
            1,
            &error
        );


    if (
        !existing.isEmpty()
        &&
        existing.value(
            QStringLiteral("configHash")
        ).toString()
        !=
        currentConfigHash()
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("检测配置不一致"),
            QStringLiteral(
                "端口 19090 上运行的是其他工程或旧配置。"
                "请先停止现有检测服务，再启动当前工程。"
            )
        );

        return;
    }


    if (!existing.isEmpty())
    {
        statusBar()->showMessage(
            QStringLiteral(
                "system-managerd 已经运行"
            ),
            3000
        );

        pollManager();

        return;
    }


    if (
        currentDatabase_.isEmpty()
        ||
        currentScriptsRoot_.isEmpty()
    )
    {
        QMessageBox::warning(
            this,
            QStringLiteral("没有工程"),
            QStringLiteral(
                "请先新建或打开一个工程。"
            )
        );

        return;
    }


    bool runnable = false;


    const QString checkConnection =
        QStringLiteral(
            "run_check_%1"
        )
        .arg(
            QCoreApplication::applicationPid()
        );


    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"),
                checkConnection
            );


        db.setDatabaseName(
            currentDatabase_
        );


        if (db.open())
        {
            QSqlQuery query(db);

            if (
                query.exec(
                    QStringLiteral(
                        "SELECT "
                        "(SELECT COUNT(*) FROM python_task WHERE enabled=1), "
                        "(SELECT COUNT(*) FROM point WHERE enabled=1)"
                    )
                )
                &&
                query.next()
            )
            {
                runnable =
                    query.value(0).toInt() > 0
                    &&
                    query.value(1).toInt() > 0;
            }

            db.close();
        }
    }


    QSqlDatabase::removeDatabase(
        checkConnection
    );


    if (!runnable)
    {
        QMessageBox::information(
            this,
            QStringLiteral("工程尚未配置"),
            QStringLiteral(
                "请先在“采集设备 → Python 通信 → 脚本任务”中新建脚本并配置点。"
            )
        );

        return;
    }


    const QDir root(
        workspaceRoot()
    );


    const QString demo =
        root.filePath(
            QStringLiteral(
                "python-communication-demo"
            )
        );


    const QString executable =
        QDir(demo).filePath(
            QStringLiteral(
                "build/system-managerd"
            )
        );


    const QString database =
        currentDatabase_;


    if (
        !QFileInfo(executable).isExecutable()
        ||
        !QFileInfo::exists(database)
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("无法启动检测"),
            QStringLiteral(
                "缺少 system-managerd 或 SQLite 配置。\n"
                "请先执行：\n"
                "cd %1 && make demo-db && make"
            )
            .arg(demo)
        );

        return;
    }


    if (managerProcess_)
    {
        managerProcess_->deleteLater();
    }


    managerProcess_ =
        new QProcess(this);


    managerProcess_->setWorkingDirectory(
        demo
    );


    managerProcess_->setProcessChannelMode(
        QProcess::MergedChannels
    );


    connect(
        managerProcess_,
        QOverload<
            int,
            QProcess::ExitStatus
        >::of(
            &QProcess::finished
        ),
        this,
        [this](int code)
        {
            statusBar()->showMessage(
                QStringLiteral(
                    "system-managerd 已退出，代码 %1"
                )
                .arg(code),
                5000
            );

            pollManager();
        }
    );


    const QString shmName =
        QStringLiteral(
            "/pycom_ui_%1"
        )
        .arg(
            QCoreApplication::applicationPid()
        );


    QDir(
        currentProjectDirectory_
    )
    .mkpath(
        QStringLiteral("run")
    );


    managerProcess_->start(
        executable,
        {
            QStringLiteral("--database"),
            database,

            QStringLiteral("--scripts-root"),
            currentScriptsRoot_,

            QStringLiteral("--shm-name"),
            shmName,

            QStringLiteral("--log"),
            QDir(
                currentProjectDirectory_
            )
            .filePath(
                QStringLiteral(
                    "run/runtime.log"
                )
            ),

            QStringLiteral("--python"),
            QStringLiteral("python3"),

            QStringLiteral("--runner"),
            QDir(demo)
                .filePath(
                    QStringLiteral(
                        "runner/runner.py"
                    )
                ),

            QStringLiteral("--tcp-host"),
            QStringLiteral("127.0.0.1"),

            QStringLiteral("--tcp-port"),
            QStringLiteral("19090")
        }
    );


    if (
        !managerProcess_
            ->waitForStarted(1500)
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("启动失败"),
            managerProcess_->errorString()
        );

        return;
    }


    statusBar()->showMessage(
        QStringLiteral(
            "正在启动 Python system-managerd..."
        ),
        2500
    );


    QTimer::singleShot(
        500,
        this,
        [this]
        {
            pollManager();
        }
    );
}

    void stopManager()
{
    // 停止 Modbus 检测

    if (
        currentCommunicationType_
        ==
        QStringLiteral("modbus")
    )
    {
        if (
            modbusTimer_
            &&
            modbusTimer_->isActive()
        )
        {
            modbusTimer_->stop();

// 在线数据：停止检测后，将质量状态改为“已停止”
//
// 注意：
// 实时值保留最后一次值
// 采集时间保留最后一次时间
// 只改变“质量”列

if (onlineTable_)
{
    for (
        int row = 0;
        row < onlineTable_->rowCount();
        ++row
    )
    {
        QTableWidgetItem *qualityItem =
            onlineTable_->item(
                row,
                8
            );

        if (!qualityItem)
        {
            qualityItem =
                new QTableWidgetItem;

            onlineTable_->setItem(
                row,
                8,
                qualityItem
            );
        }

        qualityItem->setText(
            QStringLiteral("已停止")
        );

        qualityItem->setForeground(
            QBrush(
                QColor("#808080")
            )
        );

        qualityItem->setToolTip(
            QStringLiteral(
                "Modbus 检测已停止，当前显示的是最后一次采集结果。"
            )
        );
    }
}


// 系统总览：停止检测后同步运行状态
//
// 原来：
// ●运行 | ... | 100.0% | 运行正常
//
// 停止后：
// ○停止 | ... | -- | 已停止

if (overview_)
{
    for (
        int row = 0;
        row < overview_->rowCount();
        ++row
    )
    {
        QTableWidgetItem *channelItem =
            overview_->item(
                row,
                1
            );

        if (
            !channelItem
            ||
            channelItem->text()
            !=
            QStringLiteral("Modbus 通信")
        )
        {
            continue;
        }


        // 第0列：状态

        QTableWidgetItem *statusItem =
            overview_->item(
                row,
                0
            );

        if (!statusItem)
        {
            statusItem =
                new QTableWidgetItem;

            overview_->setItem(
                row,
                0,
                statusItem
            );
        }

        statusItem->setText(
            QStringLiteral("○ 停止")
        );

        statusItem->setForeground(
            QBrush(
                QColor("#808080")
            )
        );


        // 第5列：成功率
        //
        // 停止检测时不能继续显示 100.0%
        // 因为当前根本没有进行采集

        QTableWidgetItem *successItem =
            overview_->item(
                row,
                5
            );

        if (!successItem)
        {
            successItem =
                new QTableWidgetItem;

            overview_->setItem(
                row,
                5,
                successItem
            );
        }

        successItem->setText(
            QStringLiteral("--")
        );

        successItem->setForeground(
            QBrush(
                QColor("#808080")
            )
        );


        // 第6列：响应

        QTableWidgetItem *responseItem =
            overview_->item(
                row,
                6
            );

        if (!responseItem)
        {
            responseItem =
                new QTableWidgetItem;

            overview_->setItem(
                row,
                6,
                responseItem
            );
        }

        responseItem->setText(
            QStringLiteral("已停止")
        );

        responseItem->setForeground(
            QBrush(
                QColor("#808080")
            )
        );

        break;
    }
}

            if (serviceStatus_)
            {
                serviceStatus_->setText(
                    QStringLiteral(
                        "○ Modbus 检测已停止"
                    )
                );

                serviceStatus_->setStyleSheet(
                    QStringLiteral(
                        "color:#800000;"
                    )
                );
            }


            statusBar()->showMessage(
                QStringLiteral(
                    "Modbus 检测已停止。"
                ),
                3000
            );
        }
        else
        {
            statusBar()->showMessage(
                QStringLiteral(
                    "Modbus 检测未运行。"
                ),
                3000
            );
        }

        if (modbusCollectorProcess_ &&
            modbusCollectorProcess_->state() != QProcess::NotRunning)
        {
            modbusCollectorProcess_->terminate();
            if (!modbusCollectorProcess_->waitForFinished(2000))
                modbusCollectorProcess_->kill();
        }


        return;
    }


    // 停止 Python 检测

    if (
        !managerProcess_
        ||
        managerProcess_->state()
        ==
        QProcess::NotRunning
    )
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Python 检测服务未运行。"
            ),
            3000
        );

        return;
    }


    managerProcess_->terminate();


    if (
        !managerProcess_
            ->waitForFinished(2000)
    )
    {
        managerProcess_->kill();
    }


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
        " 数据来源：ModbusCollector 本机采集服务（设备侧支持 TCP / RTU）"
        "    |    界面刷新周期：500 ms"
        "    |    各 Modbus 任务按各自配置的采集周期独立采集"
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


    // Modbus 在线数据表

    onlineTable_ = new QTableWidget(0, 10);

onlineTable_->setHorizontalHeaderLabels({
    QStringLiteral("点ID"),
    QStringLiteral("点名"),
    QStringLiteral("Slave ID"),
    QStringLiteral("Modbus地址"),
    QStringLiteral("读写方向"),
    QStringLiteral("数据类型"),
    QStringLiteral("大小端转换"),
    QStringLiteral("实时值"),
    QStringLiteral("质量"),
    QStringLiteral("采集时间")
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


    // 列宽

    onlineTable_->setColumnWidth(0, 70);    // 点ID
    onlineTable_->setColumnWidth(1, 150);   // 点名
    onlineTable_->setColumnWidth(2, 80);    // Slave ID
    onlineTable_->setColumnWidth(3, 100);   // Modbus地址
    onlineTable_->setColumnWidth(4, 100);   // 读写方向
    onlineTable_->setColumnWidth(5, 100);   // 数据类型
    onlineTable_->setColumnWidth(6, 110);   // 大小端转换
    onlineTable_->setColumnWidth(7, 120);   // 实时值
    onlineTable_->setColumnWidth(8, 80);    // 质量
    onlineTable_->setColumnWidth(9, 190);   // 采集时间


    // 点名可以自动扩展
    onlineTable_->horizontalHeader()->setSectionResizeMode(
        1,
        QHeaderView::Stretch
    );


    // 把表格加入页面

    layout->addWidget(onlineTable_);


    // 右键菜单

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


            // 任务分组标题行不允许执行强制值
            //
            // 普通点行：
            //     Qt::UserRole + 1 = "point"
            //
            // 任务标题行：
            //     Qt::UserRole + 1 = "task_header"

            QTableWidgetItem *firstItem =
                onlineTable_->item(
                    row,
                    0
                );

            if (
                !firstItem
                ||
                firstItem->data(
                    Qt::UserRole + 1
                ).toString()
                    == QStringLiteral("task_header")
            )
            {
                return;
            }


            QMenu menu(onlineTable_);


            // 当前逻辑点是否处于强制状态

            const bool forced =
                firstItem->data(
                    Qt::UserRole + 2
                ).toBool();

            const QString pointDirection =
                onlineTable_->item(row, 4)
                    ? onlineTable_->item(row, 4)->text()
                    : QString();

            const bool isOutput =
                pointDirection == QStringLiteral("OUTPUT");


            // 强制值
            //
            // 即使已经处于强制状态，也允许再次强制，
            // 用于修改当前 forcedValue。

            auto *force =
                menu.addAction(
                    pixelIcon(QColor("#800000"), 2),
                    isOutput
                        ? QStringLiteral("写入设备...")
                        : QStringLiteral("强制值...")
                );


            // 解除强制
            //
            // 只有 forced == true 时才显示。

            QAction *releaseForce =
                nullptr;

            if (forced && !isOutput)
            {
                releaseForce =
                    menu.addAction(
                        pixelIcon(QColor("#008000"), 2),
                        QStringLiteral("解除强制")
                    );
            }


            // 显示菜单

            QAction *selectedAction =
                menu.exec(
                    onlineTable_->viewport()
                        ->mapToGlobal(position)
                );


            if (
                selectedAction ==
                force
            )
            {
                forceOnlineValue(row);
            }
            else if (
                releaseForce &&
                selectedAction ==
                releaseForce
            )
            {
                releaseOnlineValue(row);
            }
        }
    );


    return page;
}

// releaseOnlineValue
//
// 解除 Modbus 逻辑点强制。
//
// 这里只向 ModbusServer 发送：
//     MSG_RELEASE_FORCE = 4
//
// ModbusServer 只清除共享内存：
//     forced      = 0
//     forcedValue = 0
//
// 不向真实 Modbus 设备发送 FC06 / FC16。

void releaseOnlineValue(int row)
{
    if (
        !onlineTable_ ||
        row < 0 ||
        row >= onlineTable_->rowCount()
    )
    {
        return;
    }


    // 目前“解除强制”只用于 Modbus 工程

    if (
        currentCommunicationType_
        !=
        QStringLiteral("modbus")
    )
    {
        return;
    }


    QTableWidgetItem *idItem =
        onlineTable_->item(
            row,
            0
        );

    QTableWidgetItem *nameItem =
        onlineTable_->item(
            row,
            1
        );


    if (
        !idItem ||
        !nameItem
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("解除强制失败"),
            QStringLiteral(
                "当前点数据不完整"
            )
        );

        return;
    }


    // 获取逻辑点身份
    //
    // pointId:
    //     表格第 0 列
    //
    // taskId:
    //     第 0 列 Qt::UserRole

    const int pointId =
        idItem->text().toInt();

    const int taskId =
        idItem->data(
            Qt::UserRole
        ).toInt();

    const QString name =
        nameItem->text();


    if (
        taskId <= 0 ||
        pointId <= 0
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("解除强制失败"),
            QStringLiteral(
                "当前点缺少有效的 Modbus 任务ID或点ID"
            )
        );

        return;
    }


    // 再确认一次

    if (
        QMessageBox::question(
            this,
            QStringLiteral("确认解除强制"),
            QStringLiteral(
                "确定解除点“%1”的强制状态吗？\n\n"
                "解除后，该点将恢复显示真实 Modbus 采集值。"
            ).arg(
                name
            ),
            QMessageBox::Yes |
            QMessageBox::No,
            QMessageBox::No
        )
        !=
        QMessageBox::Yes
    )
    {
        return;
    }


    // 发送 MSG_RELEASE_FORCE = 4
    //
    // payload:
    //
    // {
    //     "taskId": 1,
    //     "pointId": 1
    // }

    QString error;

    const QJsonObject response =
        modbusServerClient_.request(
            4,
            modbusRequestId_,
            &error,
            QJsonObject{
                {
                    QStringLiteral("taskId"),
                    taskId
                },
                {
                    QStringLiteral("pointId"),
                    pointId
                }
            }
        );


    // 检查返回

    if (
        !response.value(
            QStringLiteral("ok")
        ).toBool()
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("解除强制失败"),
            response.value(
                QStringLiteral("message")
            ).toString(
                response.value(
                    QStringLiteral("error")
                ).toString(
                    error
                )
            )
        );

        return;
    }


    // 成功

    statusBar()->showMessage(
        QStringLiteral(
            "已解除 Modbus 点 %1 的强制状态"
        ).arg(
            name
        ),
        4000
    );


    // 立即重新读取 Snapshot
    //
    // 注意：
    // 如果刚解除时下一轮 Modbus 采集尚未发生，
    // value 有可能短暂保留原强制值。
    //
    // 随后正常采集周期到达后，
    // 实际值会自动恢复。

    pollModbusServer();
}

    void forceOnlineValue(int row)
{
    if (
        !onlineTable_ ||
        row < 0 ||
        row >= onlineTable_->rowCount()
    )
    {
        return;
    }


    // Modbus 工程

    if (
        currentCommunicationType_ ==
        QStringLiteral("modbus")
    )
    {
        QTableWidgetItem *idItem =
            onlineTable_->item(row, 0);

        QTableWidgetItem *nameItem =
            onlineTable_->item(row, 1);

        QTableWidgetItem *typeItem =
            onlineTable_->item(row, 5);

        QTableWidgetItem *valueItem =
            onlineTable_->item(row, 7);

        QTableWidgetItem *directionItem =
            onlineTable_->item(row, 4);


        if (
            !idItem ||
            !nameItem ||
            !typeItem ||
            !valueItem ||
            !directionItem
        )
        {
            QMessageBox::critical(
                this,
                QStringLiteral("操作失败"),
                QStringLiteral("当前点数据不完整")
            );

            return;
        }


        const int pointId =
            idItem->text().toInt();

        const int taskId =
            idItem->data(
                Qt::UserRole
            ).toInt();


        if (
            taskId <= 0 ||
            pointId <= 0
        )
        {
            QMessageBox::critical(
                this,
                QStringLiteral("强制失败"),
                QStringLiteral(
                    "当前点缺少有效的 Modbus 任务ID或点ID"
                )
            );

            return;
        }


        const QString name =
            nameItem->text();

        const QString type =
            typeItem->text();

        const QString oldValue =
            valueItem->text();

        const bool isOutput =
            directionItem->text() == QStringLiteral("OUTPUT");


        // 输入强制值

        bool accepted = false;

        const QString value =
            QInputDialog::getText(
                this,
                isOutput
                    ? QStringLiteral("写入 Modbus 设备")
                    : QStringLiteral("强制点值"),
                QStringLiteral(
                    "点：%1（%2）\n%3："
                ).arg(
                    name,
                    type,
                    isOutput
                        ? QStringLiteral("输入设备输出值")
                        : QStringLiteral("输入强制值")
                ),
                QLineEdit::Normal,
                oldValue,
                &accepted
            ).trimmed();


        if (
            !accepted ||
            value.isEmpty()
        )
        {
            return;
        }


        // 确认

        if (
            QMessageBox::warning(
                this,
                isOutput
                    ? QStringLiteral("确认写入设备")
                    : QStringLiteral("确认强制"),
                isOutput
                    ? QStringLiteral(
                        "即将向真实 Modbus 设备写入：\n"
                        "%1 = %2\n\n"
                        "该操作会立即改变设备寄存器，请确认现场设备状态安全。"
                      ).arg(name, value)
                    : QStringLiteral(
                        "将强制 Modbus 逻辑点的显示值：\n"
                        "%1 = %2\n\n"
                        "该操作仅修改共享内存中的逻辑值。"
                      ).arg(name, value),
                QMessageBox::Yes |
                QMessageBox::No,
                QMessageBox::No
            )
            != QMessageBox::Yes
        )
        {
            return;
        }


        // 发送 MSG_FORCE_VALUE = 3
        //
        // ModbusServer:
        //     127.0.0.1:19091

        QString error;

        const QJsonObject response =
            modbusServerClient_.request(
                3,
                modbusRequestId_,
                &error,
                QJsonObject{
                    {
                        QStringLiteral("taskId"),
                        taskId
                    },
                    {
                        QStringLiteral("pointId"),
                        pointId
                    },
                    {
                        QStringLiteral("name"),
                        name
                    },
                    {
                        QStringLiteral("value"),
                        value
                    }
                }
            );


        if (
            !response.value(
                QStringLiteral("ok")
            ).toBool()
        )
        {
            QMessageBox::critical(
                this,
                isOutput
                    ? QStringLiteral("写入设备失败")
                    : QStringLiteral("强制失败"),
                response.value(
                    QStringLiteral("message")
                ).toString(
                    response.value(
                        QStringLiteral("error")
                    ).toString(
                        error
                    )
                )
            );

            return;
        }


        statusBar()->showMessage(
            (isOutput
                ? QStringLiteral("已写入 Modbus 设备 %1 = %2")
                : QStringLiteral("已强制 Modbus 逻辑点 %1 = %2"))
            .arg(
                name,
                value
            ),
            4000
        );

        // OUTPUT 的 FC06/FC16 使用独立写连接，不属于周期采集快照。
        // 直接显示服务端返回的真实写请求和写响应，避免被下一轮 FC03 覆盖。
        if (isOutput && frames_ && !framePaused_)
        {
            const QString timestamp = response.value(QStringLiteral("timestamp")).toString();
            const QString tx = response.value(QStringLiteral("txTelegram")).toString();
            const QString rx = response.value(QStringLiteral("rxTelegram")).toString();
            const QString transport = response.value(QStringLiteral("transport")).toString(QStringLiteral("TCP"));
            appendModbusFrame(timestamp, name, QStringLiteral("RX"), rx, transport);
            appendModbusFrame(timestamp, name, QStringLiteral("TX"), tx, transport);
            const QString monitorKey = QStringLiteral("%1:%2").arg(taskId).arg(pointId);
            lastModbusFrameKeys_.insert(
                monitorKey,
                timestamp + QStringLiteral("|") + tx + QStringLiteral("|") + rx
            );
        }


        // 强制完成后立即重新读取
        pollModbusServer();

        return;
    }


    // Python 工程
    //
    // 保持原来的 system-managerd 强制逻辑

    const QString pointId =
        onlineTable_->item(
            row,
            0
        )->text();


    const QString name =
        onlineTable_->item(
            row,
            1
        )->text();


    const QString type =
        onlineTable_->item(
            row,
            5
        )->text();


    const QString oldValue =
        onlineTable_->item(
            row,
            6
        )->text();


    bool accepted = false;


    const QString value =
        QInputDialog::getText(
            this,
            QStringLiteral("强制点值"),
            QStringLiteral(
                "点：%1（%2）\n输入强制值："
            ).arg(
                name,
                type
            ),
            QLineEdit::Normal,
            oldValue,
            &accepted
        ).trimmed();


    if (
        !accepted ||
        value.isEmpty()
    )
    {
        return;
    }


    if (
        QMessageBox::warning(
            this,
            QStringLiteral("确认强制"),
            QStringLiteral(
                "将通过 system-managerd 修改共享内存中的点值：\n"
                "%1 = %2\n\n"
                "该操作会写入运行日志。"
            ).arg(
                name,
                value
            ),
            QMessageBox::Yes |
            QMessageBox::No,
            QMessageBox::No
        )
        != QMessageBox::Yes
    )
    {
        return;
    }


    QString error;


    const QJsonObject response =
        queryManager(
            3,
            &error,
            QJsonObject{
                {
                    QStringLiteral("pointId"),
                    pointId.toInt()
                },
                {
                    QStringLiteral("value"),
                    value
                },
                {
                    QStringLiteral("configHash"),
                    currentConfigHash()
                }
            }
        );


    if (
        !response.value(
            QStringLiteral("ok")
        ).toBool()
    )
    {
        QMessageBox::critical(
            this,
            QStringLiteral("强制失败"),
            response.value(
                QStringLiteral("message")
            ).toString(
                response.value(
                    QStringLiteral("error")
                ).toString(
                    error
                )
            )
        );

        return;
    }


    statusBar()->showMessage(
        QStringLiteral(
            "已强制 %1 = %2"
        ).arg(
            name,
            value
        ),
        4000
    );


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
                pollModbusServer();
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
        pollModbusServer();
    else
        pollManager();
}

void showModbusPoints()
{
    // 如果已经打开，就直接切换过去

    for (
        int index = 0;
        index < tabs_->count();
        ++index
    )
    {
        if (
            tabs_->tabText(index)
            ==
            QStringLiteral("Modbus 点表")
        )
        {
            tabs_->setCurrentIndex(
                index
            );

            // 每次重新点击左侧“点表”时重新读取数据库
            loadModbusPointsTable();

            return;
        }
    }


    // 创建页面

    QWidget *page =
    createModbusPointsPage();

    // 从 modbus_config.db 加载现有点
    loadModbusPointsTable();

    const int index =
        tabs_->addTab(
            page,
            pixelIcon(
                QColor("#008080"),
                1
            ),
            QStringLiteral("Modbus 点表")
        );

    tabs_->setCurrentIndex(
        index
    );
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


    void pollManager()
{
    // Modbus 工程使用 ModbusCollector + ModbusServer TCP
    // 不使用 system-managerd 的实时数据 Snapshot

    if (currentCommunicationType_ == QStringLiteral("modbus"))
    {
        return;
    }
    QString error;

    // 1. 检查 system-managerd 是否在线

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


    // 2. 检查配置是否匹配

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


    // 3. 显示服务状态

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


    // 4. 没有在线数据表则结束

    if (!onlineTable_)
        return;


    // 5. 请求实时数据 Snapshot

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


    // 6. 获取 points 数组

    const QJsonArray points =
        snapshot.value(
            QStringLiteral("points")
        ).toArray();


    // 7. 更新 Qt 在线数据表

    onlineTable_->setRowCount(points.size());


    for (
        int row = 0;
        row < points.size();
        ++row
    ) {

        const QJsonObject point =
            points.at(row).toObject();


        // 时间戳

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


        // 获取字段

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


        // 写入表格

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


            // 状态颜色

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


            // 实时值颜色

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


            // 时间戳 Tooltip

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
    // Modbus 工程不使用 Python Runner 状态刷新，
    // 防止把 Modbus 系统总览的 1 / 1 覆盖成 0 / 0。
    if (currentCommunicationType_ == QStringLiteral("modbus"))
        return;

    if (!frames_ && !overview_)
        return;

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

    void pollRuntimeLog()
    {
        if (!runtimeLog_)
            return;

        if (currentProjectDirectory_.isEmpty())
            return;

        const QString logPath =
            QDir(currentProjectDirectory_)
                .filePath(QStringLiteral("run/runtime.log"));

        QFile file(logPath);

        if (!file.exists())
            return;

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        const qint64 fileSize = file.size();

        // 如果日志文件被清空、重新创建或截断，则从头开始读取
        if (fileSize < runtimeLogOffset_)
        {
            runtimeLogOffset_ = 0;
            runtimeLogPending_.clear();
            runtimeLog_->clear();
        }

        if (!file.seek(runtimeLogOffset_))
            return;

        const QByteArray data = file.readAll();

        runtimeLogOffset_ = file.pos();

        if (data.isEmpty())
            return;

        runtimeLogPending_ += QString::fromUtf8(data);

        // 按行处理，保留最后一段不完整的日志
        const QStringList lines =
            runtimeLogPending_.split(
                QRegularExpression(QStringLiteral("[\\r\\n]+")),
                Qt::KeepEmptyParts
            );

        if (lines.isEmpty())
            return;

        // 最后一段可能还没有写完，暂时保留
        runtimeLogPending_ = lines.last();

        for (int i = 0; i < lines.size() - 1; ++i)
        {
            const QString line = lines.at(i);

            if (line.isEmpty())
                continue;

            runtimeLog_->appendPlainText(line);
        }

        // 自动滚动到底部
        QTextCursor cursor = runtimeLog_->textCursor();
        cursor.movePosition(QTextCursor::End);
        runtimeLog_->setTextCursor(cursor);
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

    void applyFrameFilters()
    {
        if (!frames_) return;
        const QString source = frameSourceFilter_ ? frameSourceFilter_->currentText() : QStringLiteral("全部");
        const QString direction = frameDirectionFilter_ ? frameDirectionFilter_->currentText() : QStringLiteral("全部");
        const int functionIndex = frameFunctionFilter_ ? frameFunctionFilter_->currentIndex() : 0;
        const QStringList functions = {QString(), QStringLiteral("03"), QStringLiteral("04"),
                                       QStringLiteral("06"), QStringLiteral("16")};
        for (int row = 0; row < frames_->rowCount(); ++row) {
            const QString rowSource = frames_->item(row, 1) ? frames_->item(row, 1)->data(Qt::UserRole).toString() : QString();
            const QString rowFunction = frames_->item(row, 3) ? frames_->item(row, 3)->data(Qt::UserRole).toString() : QString();
            const QString rowDirection = frames_->item(row, 6) ? frames_->item(row, 6)->text() : QString();
            const bool sourceOk = source == QStringLiteral("全部") ||
                (source == QStringLiteral("Modbus") && rowSource == QStringLiteral("MODBUS")) ||
                (source == QStringLiteral("Python") && rowSource == QStringLiteral("PY"));
            const bool directionOk = direction == QStringLiteral("全部") || rowDirection == direction;
            bool functionOk = functionIndex == 0;
            if (functionIndex >= 1 && functionIndex <= 4) functionOk = rowFunction == functions.at(functionIndex);
            else if (functionIndex == 5) functionOk = rowFunction == QStringLiteral("06") || rowFunction == QStringLiteral("16");
            frames_->setRowHidden(row, !(sourceOk && directionOk && functionOk));
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
            QStringLiteral("—"), summary, QString::number(text.toUtf8().size()), stream
        };
        for (int column = 0; column < values.size(); ++column)
            frames_->setItem(0, column, new QTableWidgetItem(values[column]));
        frames_->item(0, 1)->setData(Qt::UserRole, QStringLiteral("PY"));
        frames_->item(0, 3)->setData(Qt::UserRole, QString());
        frames_->item(0, 4)->setData(Qt::UserRole, text);
        frames_->item(0, 4)->setToolTip(QStringLiteral("单击查看完整内容"));
        frames_->item(0, 1)->setForeground(QColor("#800080"));
        if (stream == QStringLiteral("stderr")) {
            frames_->item(0, 4)->setForeground(QColor("#c00000"));
            frames_->item(0, 6)->setForeground(QColor("#c00000"));
        }
        applyFrameFilters();
        while (frames_->rowCount() > 500) frames_->removeRow(frames_->rowCount() - 1);
    }

void appendModbusFrame(
    const QString &timestamp,
    const QString &pointName,
    const QString &direction,
    const QString &telegram,
    const QString &transport
)
{
    if (!frames_ || framePaused_)
        return;

    if (telegram.trimmed().isEmpty())
        return;

    // 时间

    QString displayTime = timestamp;

    const QDateTime dateTime =
        QDateTime::fromString(
            timestamp,
            QStringLiteral("yyyy-MM-dd HH:mm:ss")
        );

    if (dateTime.isValid())
    {
        displayTime =
            dateTime.toString(
                QStringLiteral("HH:mm:ss")
            );
    }

    // 计算真实报文字节数
    //
    // telegram 格式：
    // 00 01 00 00 00 06 01 03 ...

    const QStringList bytes =
        telegram.split(
            QChar(' '),
            Qt::SkipEmptyParts
        );

    const int byteLength =
        bytes.size();

    const bool isRtu = transport.compare(QStringLiteral("RTU"), Qt::CaseInsensitive) == 0;
    const int unitIndex = isRtu ? 0 : 6;
    const int functionIndex = isRtu ? 1 : 7;
    const int payloadIndex = isRtu ? 2 : 8;
    // TCP 含 7 字节 MBAP；RTU 从站地址和功能码从第 1、2 字节开始。
    bool functionOk = false;
    int functionValue = bytes.size() > functionIndex ? bytes.at(functionIndex).toInt(&functionOk, 16) : -1;
    const bool exceptionResponse = functionOk && (functionValue & 0x80);
    if (exceptionResponse) functionValue &= 0x7f;
    const QString functionCode = functionOk
        ? QStringLiteral("%1").arg(functionValue == 0x10 ? 16 : functionValue, 2, 10, QChar('0'))
        : QStringLiteral("—");
    QString functionName;
    switch (functionValue) {
    case 0x03: functionName = QStringLiteral("读保持寄存器"); break;
    case 0x04: functionName = QStringLiteral("读输入寄存器"); break;
    case 0x06: functionName = QStringLiteral("写单寄存器"); break;
    case 0x10: functionName = QStringLiteral("写多寄存器"); break;
    default: functionName = QStringLiteral("未知功能"); break;
    }
    if (exceptionResponse) functionName += QStringLiteral("（异常响应）");

    auto hexWord = [&bytes](int index, bool *ok = nullptr) {
        bool highOk = false, lowOk = false;
        const int high = bytes.size() > index ? bytes.at(index).toInt(&highOk, 16) : 0;
        const int low = bytes.size() > index + 1 ? bytes.at(index + 1).toInt(&lowOk, 16) : 0;
        if (ok) *ok = highOk && lowOk;
        return (high << 8) | low;
    };
    bool unitOk = false;
    const int unitId = bytes.size() > unitIndex ? bytes.at(unitIndex).toInt(&unitOk, 16) : 0;
    bool addressOk = false;
    const int address = hexWord(payloadIndex, &addressOk);
    bool fieldOk = false;
    const int field = hexWord(payloadIndex + 2, &fieldOk);
    const int registerNumber = address + (functionValue == 0x04 ? 30001 : 40001);

    QString summary = QStringLiteral("Unit %1  ·  FC%2 %3").arg(unitOk ? QString::number(unitId) : QStringLiteral("?"), functionCode, functionName);
    if (direction == QStringLiteral("TX") && addressOk && fieldOk) {
        summary += QStringLiteral("  ·  地址 %1").arg(registerNumber);
        if (functionValue == 0x03 || functionValue == 0x04 || functionValue == 0x10)
            summary += QStringLiteral("  ·  数量 %1").arg(field);
        else if (functionValue == 0x06)
            summary += QStringLiteral("  ·  值 %1").arg(field);
    }

    QString detail = QStringLiteral("MODBUS %1 %2\n点名：%3\nUnit ID：%4\n功能码：FC%5 %6\n长度：%7 字节")
        .arg(isRtu ? QStringLiteral("RTU") : QStringLiteral("TCP"), direction, pointName,
             unitOk ? QString::number(unitId) : QStringLiteral("未知"),
             functionCode, functionName, QString::number(byteLength));
    if (addressOk && (direction == QStringLiteral("TX") || functionValue == 0x06 || functionValue == 0x10))
        detail += QStringLiteral("\n寄存器：%1（协议地址 %2）").arg(registerNumber).arg(address);
    detail += QStringLiteral("\n\n原始报文：\n%1").arg(telegram);

    // 插入最上面

    frames_->insertRow(0);

    const QStringList values = {
        displayTime,
        isRtu ? QStringLiteral("RTU") : QStringLiteral("TCP"),
        pointName,
        QStringLiteral("FC%1 %2").arg(functionCode, functionName),
        summary,
        QString::number(byteLength),
        direction
    };

    for (
        int column = 0;
        column < values.size();
        ++column
    )
    {
        frames_->setItem(
            0,
            column,
            new QTableWidgetItem(
                values[column]
            )
        );
    }

    // 完整报文放进 UserRole，供下面详情框查看
    frames_->item(0, 1)->setData(Qt::UserRole, QStringLiteral("MODBUS"));
    frames_->item(0, 3)->setData(Qt::UserRole, functionCode);
    frames_->item(0, 4)->setData(
        Qt::UserRole,
        detail
    );

    frames_->item(0, 4)->setToolTip(
        isRtu ? QStringLiteral("单击查看完整 Modbus RTU 报文（含 CRC）")
              : QStringLiteral("单击查看完整 Modbus TCP 报文")
    );

    // 颜色
    // TX = 蓝色
    // RX = 绿色

    if (direction == QStringLiteral("TX"))
    {
        frames_->item(0, 1)->setForeground(
            QColor("#0000c0")
        );

        frames_->item(0, 6)->setForeground(
            QColor("#0000c0")
        );
        if (functionValue == 0x06 || functionValue == 0x10)
            frames_->item(0, 3)->setBackground(QColor("#fff0c0"));
    }
    else if (direction == QStringLiteral("RX"))
    {
        frames_->item(0, 1)->setForeground(
            QColor("#008000")
        );

        frames_->item(0, 6)->setForeground(
            QColor("#008000")
        );
    }

    if (exceptionResponse) {
        for (int column = 0; column < frames_->columnCount(); ++column)
            frames_->item(0, column)->setForeground(QColor("#c00000"));
    }

    applyFrameFilters();

    // 最多保留 500 条
    while (frames_->rowCount() > 500)
    {
        frames_->removeRow(
            frames_->rowCount() - 1
        );
    }
}

void pollModbusServer()
{
    if (!onlineTable_)
        return;


    QString error;


    // 通过 TCP 19091 向 ModbusCollector 请求 SNAPSHOT

    const QJsonObject response =
        modbusServerClient_.request(
            2,                  // MSG_SNAPSHOT
            modbusRequestId_,
            &error
        );


if (response.isEmpty())
{
    // ModbusServer TCP 已断开
    //
    // 注意：
    // 不清空最后一次采集到的值。
    // 保留点名、地址、实时值、采集时间等最后快照，
    // 但是所有点的质量必须立即变为 BAD。

    for (
        int row = 0;
        row < onlineTable_->rowCount();
        ++row
    )
    {
        QTableWidgetItem *idItem =
            onlineTable_->item(
                row,
                0
            );


        if (!idItem)
        {
            continue;
        }


        // 跳过 Modbus 任务标题行

        if (
            idItem->data(
                Qt::UserRole + 1
            ).toString()
            ==
            QStringLiteral("task_header")
        )
        {
            continue;
        }


        // 普通点：质量改成 BAD

        QTableWidgetItem *qualityItem =
            onlineTable_->item(
                row,
                8
            );


        if (!qualityItem)
        {
            qualityItem =
                new QTableWidgetItem;

            onlineTable_->setItem(
                row,
                8,
                qualityItem
            );
        }


        qualityItem->setText(
            QStringLiteral("BAD")
        );

        qualityItem->setTextAlignment(
            Qt::AlignCenter
        );

        qualityItem->setForeground(
            QBrush(
                QColor("#c00000")
            )
        );


        QFont qualityFont =
            qualityItem->font();

        qualityFont.setBold(
            true
        );

        qualityItem->setFont(
            qualityFont
        );
    }


    // 系统总览改为通信异常

    if (
        currentCommunicationType_
            == QStringLiteral("modbus")
        &&
        overview_
    )
    {
        for (
            int row = 0;
            row < overview_->rowCount();
            ++row
        )
        {
            QTableWidgetItem *channelItem =
                overview_->item(
                    row,
                    1
                );


            if (
                !channelItem
                ||
                channelItem->text()
                    != QStringLiteral("Modbus 通信")
            )
            {
                continue;
            }


            // 状态

            if (
                overview_->item(
                    row,
                    0
                )
            )
            {
                overview_->item(
                    row,
                    0
                )->setText(
                    QStringLiteral("● 异常")
                );

                overview_->item(
                    row,
                    0
                )->setForeground(
                    QColor("#c00000")
                );
            }


            // 成功率

            if (
                overview_->item(
                    row,
                    5
                )
            )
            {
                overview_->item(
                    row,
                    5
                )->setText(
                    QStringLiteral("0.0%")
                );

                overview_->item(
                    row,
                    5
                )->setForeground(
                    QColor("#c00000")
                );
            }


            // 响应

            if (
                overview_->item(
                    row,
                    6
                )
            )
            {
                overview_->item(
                    row,
                    6
                )->setText(
                    QStringLiteral(
                        "Modbus 采集服务未连接"
                    )
                );

                overview_->item(
                    row,
                    6
                )->setForeground(
                    QColor("#c00000")
                );
            }


            break;
        }
    }


    // 状态栏保留具体 TCP 错误

    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 采集服务未连接：%1"
        ).arg(error)
    );


    return;
}


    // 检查服务器返回 ok

    if (
        !response.value(
            QStringLiteral("ok")
        ).toBool()
    )
    {
        statusBar()->showMessage(
            QStringLiteral(
                "Modbus 采集服务返回错误：%1"
            )
            .arg(
                response.value(
                    QStringLiteral("error")
                ).toString()
            )
        );

        return;
    }

    // 获取 Modbus 任务配置
    //
    // tasks:
    //     ModbusCollector Snapshot 返回的任务配置
    //
    // variables:
    //     当前实时变量快照

    const QJsonArray tasks =
        response.value(
            QStringLiteral("tasks")
        ).toArray();

    // 获取 variables

    const QJsonArray variables =
        response.value(
            QStringLiteral("variables")
        ).toArray();

// 更新 Modbus 系统总览

if (
    currentCommunicationType_ == QStringLiteral("modbus")
    &&
    overview_
)
{
    // 如果总览中还没有 Modbus 行，则创建

    int modbusRow = -1;

    for (int row = 0; row < overview_->rowCount(); ++row)
    {
        QTableWidgetItem *channel =
            overview_->item(row, 1);

        if (
            channel
            &&
            channel->text() == QStringLiteral("Modbus 通信")
        )
        {
            modbusRow = row;
            break;
        }
    }

    if (modbusRow < 0)
    {
        modbusRow = overview_->rowCount();
        overview_->insertRow(modbusRow);

        const QStringList values = {
            QStringLiteral("● 运行"),
            QStringLiteral("Modbus 通信"),
            QStringLiteral("Modbus TCP / 主站"),
            QStringLiteral("127.0.0.1:502"),
            QStringLiteral("0 / 0"),
            QStringLiteral("--"),
            QStringLiteral("运行正常")
        };

        for (int column = 0; column < values.size(); ++column)
        {
            overview_->setItem(
                modbusRow,
                column,
                new QTableWidgetItem(values[column])
            );
        }
    }


    // 状态

    overview_->item(
        modbusRow,
        0
    )->setText(
        QStringLiteral("● 运行")
    );

    overview_->item(
        modbusRow,
        0
    )->setForeground(
        QColor("#008000")
    );


    // 通道

    overview_->item(
        modbusRow,
        1
    )->setText(
        QStringLiteral("Modbus 通信")
    );


    // 协议 / 角色

    bool hasTcpTask = false;
    bool hasRtuTask = false;
    QStringList endpoints;
    for (const QJsonValue &taskValue : tasks) {
        const QJsonObject task = taskValue.toObject();
        const QString transport = task.value(QStringLiteral("transport")).toString(QStringLiteral("TCP"));
        if (transport == QStringLiteral("RTU")) {
            hasRtuTask = true;
            endpoints << QStringLiteral("%1@%2").arg(task.value(QStringLiteral("serialPort")).toString())
                                              .arg(task.value(QStringLiteral("baudRate")).toInt());
        } else {
            hasTcpTask = true;
            endpoints << QStringLiteral("%1:%2").arg(task.value(QStringLiteral("host")).toString())
                                               .arg(task.value(QStringLiteral("port")).toInt());
        }
    }
    const QString protocolSummary = hasTcpTask && hasRtuTask ? QStringLiteral("Modbus TCP + RTU / 主站")
        : hasRtuTask ? QStringLiteral("Modbus RTU / 主站") : QStringLiteral("Modbus TCP / 主站");
    overview_->item(
        modbusRow,
        2
    )->setText(
        protocolSummary
    );


    // 对端
    //
    // 注意：
    // 这里显示的是实际 Modbus 设备 :502，
    // 不是 Qt -> ModbusServer 的 :19091

    overview_->item(
        modbusRow,
        3
    )->setText(
        endpoints.isEmpty() ? QStringLiteral("--") : endpoints.join(QStringLiteral("; "))
    );


    // 统计 GOOD 点

    int goodCount = 0;

    for (const QJsonValue &value : variables)
    {
        const QJsonObject var =
            value.toObject();

        if (
            var.value(
                QStringLiteral("status")
            ).toInt() == 1
        )
        {
            ++goodCount;
        }
    }


    // 成功率

    double successRate = 0.0;

    if (!variables.isEmpty())
    {
        successRate =
            100.0
            * static_cast<double>(goodCount)
            / static_cast<double>(variables.size());
    }

    overview_->item(
        modbusRow,
        5
    )->setText(
        QStringLiteral("%1%")
        .arg(
            successRate,
            0,
            'f',
            1
        )
    );


    if (goodCount == variables.size() && !variables.isEmpty())
    {
        overview_->item(
            modbusRow,
            5
        )->setForeground(
            QColor("#008000")
        );

        overview_->item(
            modbusRow,
            6
        )->setText(
            QStringLiteral("运行正常")
        );

        overview_->item(
            modbusRow,
            6
        )->setForeground(
            QColor("#008000")
        );
    }
    else
    {
        overview_->item(
            modbusRow,
            5
        )->setForeground(
            QColor("#c00000")
        );

        overview_->item(
            modbusRow,
            6
        )->setText(
            QStringLiteral("存在通信异常")
        );

        overview_->item(
            modbusRow,
            6
        )->setForeground(
            QColor("#c00000")
        );
    }


    // 底部统计

    if (taskSummary_)
    {
        int enabledTaskCount = 0;

        for (const QJsonValue &taskValue : tasks)
        {
            const QJsonObject task =
                taskValue.toObject();

            if (
                task.value(
                    QStringLiteral("enable")
                ).toBool()
            )
            {
                ++enabledTaskCount;
            }
        }


        taskSummary_->setText(
            QStringLiteral("%1 / %2")
            .arg(enabledTaskCount)
            .arg(tasks.size())
        );
    }

    if (pointSummary_)
    {
        pointSummary_->setText(
            QStringLiteral("%1 点")
            .arg(variables.size())
        );
    }
}

// Modbus 在线数据按任务分组显示
//
// 显示结构：
//
// Modbus任务1 | 127.0.0.1:502 | Slave 1 | 采集周期 1500 ms
//     point 1
//     point 2
//
// Modbus任务2 | 127.0.0.1:502 | Slave 1 | 采集周期 500 ms
//     point 9
//
// 注意：
// Qt 仍然每 500 ms 请求一次 Snapshot。
// 这里显示的 period 是真正的 Modbus 任务采集周期。

onlineTable_->clearContents();
onlineTable_->setRowCount(0);


// 按 tasks[] 的顺序显示任务

for (
    int taskIndex = 0;
    taskIndex < tasks.size();
    ++taskIndex
)
{
    const QJsonObject task =
        tasks.at(
            taskIndex
        ).toObject();


    const int taskId =
        task.value(
            QStringLiteral("taskId")
        ).toInt();

    const QString taskName =
        task.value(
            QStringLiteral("name")
        ).toString();

    const QString taskHost =
        task.value(
            QStringLiteral("host")
        ).toString();

    const int taskPort =
        task.value(
            QStringLiteral("port")
        ).toInt();

    const QString taskTransport = task.value(QStringLiteral("transport")).toString(QStringLiteral("TCP"));
    const QString taskSerialPort = task.value(QStringLiteral("serialPort")).toString();
    const int taskBaudRate = task.value(QStringLiteral("baudRate")).toInt();

    const int taskSlaveId =
        task.value(
            QStringLiteral("slaveId")
        ).toInt();

    const int taskPeriod =
        task.value(
            QStringLiteral("period")
        ).toInt();

    const bool taskEnable =
        task.value(
            QStringLiteral("enable")
        ).toBool();


    // 当前任务被禁用时不显示

    if (!taskEnable)
    {
        continue;
    }


    // 插入任务标题行

    const int headerRow =
        onlineTable_->rowCount();

    onlineTable_->insertRow(
        headerRow
    );


    const QString endpoint = taskTransport == QStringLiteral("RTU")
        ? QStringLiteral("RTU %1 @ %2").arg(taskSerialPort).arg(taskBaudRate)
        : QStringLiteral("TCP %1:%2").arg(taskHost).arg(taskPort);
    const QString headerText = QStringLiteral("▼ %1    |    %2    |    Slave %3    |    采集周期 %4 ms")
        .arg(taskName, endpoint).arg(taskSlaveId).arg(taskPeriod);


    auto *headerItem =
        new QTableWidgetItem(
            headerText
        );


    // 保存任务身份

    headerItem->setData(
        Qt::UserRole,
        taskId
    );

    headerItem->setData(
        Qt::UserRole + 1,
        QStringLiteral("task_header")
    );


    // 标题字体

    QFont headerFont =
        headerItem->font();

    headerFont.setBold(
        true
    );

    headerItem->setFont(
        headerFont
    );


    // 标题颜色

    headerItem->setBackground(
        QBrush(
            QColor("#d8e8f8")
        )
    );

    headerItem->setForeground(
        QBrush(
            QColor("#000080")
        )
    );


    onlineTable_->setItem(
        headerRow,
        0,
        headerItem
    );


    // 横跨全部 10 列
    onlineTable_->setSpan(
        headerRow,
        0,
        1,
        10
    );


    onlineTable_->setRowHeight(
        headerRow,
        28
    );


    // 找出属于当前 taskId 的所有变量

    for (
        int variableIndex = 0;
        variableIndex < variables.size();
        ++variableIndex
    )
    {
        const QJsonObject var =
            variables.at(
                variableIndex
            ).toObject();


        const int variableTaskId =
            var.value(
                QStringLiteral("taskId")
            ).toInt();


        if (
            variableTaskId != taskId
        )
        {
            continue;
        }


        // 新建点数据行

        const int tableRow =
            onlineTable_->rowCount();

        onlineTable_->insertRow(
            tableRow
        );


        // Modbus TX / RX 报文监视

        if (
            frames_
            &&
            !framePaused_
        )
        {
            const QString pointName =
                var.value(
                    QStringLiteral("name")
                ).toString();

            const int framePointId =
                var.value(
                    QStringLiteral("pointId")
                ).toInt();

            const QString timestamp =
                var.value(
                    QStringLiteral("timestamp")
                ).toString();

            const QString txTelegram =
                var.value(
                    QStringLiteral("txTelegram")
                ).toString();

            const QString rxTelegram =
                var.value(
                    QStringLiteral("rxTelegram")
                ).toString();


            // taskId + pointId 唯一标识一个 Modbus 点

            const QString monitorKey =
                QStringLiteral("%1:%2")
                .arg(taskId)
                .arg(framePointId);


            const QString frameKey =
                timestamp
                + QStringLiteral("|")
                + txTelegram
                + QStringLiteral("|")
                + rxTelegram;


            const QString oldKey =
                lastModbusFrameKeys_.value(
                    monitorKey
                );


            if (
                !frameKey.isEmpty()
                &&
                frameKey != oldKey
            )
            {
                lastModbusFrameKeys_.insert(
                    monitorKey,
                    frameKey
                );


                appendModbusFrame(
                    timestamp,
                    pointName,
                    QStringLiteral("RX"),
                    rxTelegram,
                    taskTransport
                );

                appendModbusFrame(
                    timestamp,
                    pointName,
                    QStringLiteral("TX"),
                    txTelegram,
                    taskTransport
                );
            }
        }


        // 点ID
        //
        // UserRole:
        //     taskId
        //
        // UserRole + 1:
        //     point

        const int pointId =
            var.value(
                QStringLiteral("pointId")
            ).toInt();


        auto *idItem =
            new QTableWidgetItem(
                QString::number(
                    pointId
                )
            );


        idItem->setData(
            Qt::UserRole,
            taskId
        );

        idItem->setData(
            Qt::UserRole + 1,
            QStringLiteral("point")
        );


        onlineTable_->setItem(
            tableRow,
            0,
            idItem
        );


        // 点名

        onlineTable_->setItem(
            tableRow,
            1,
            new QTableWidgetItem(
                var.value(
                    QStringLiteral("name")
                ).toString()
            )
        );


        // Slave ID

        onlineTable_->setItem(
            tableRow,
            2,
            new QTableWidgetItem(
                QString::number(
                    var.value(
                        QStringLiteral("slave")
                    ).toInt()
                )
            )
        );


        // 标准 Modbus 地址

        const int function =
            var.value(
                QStringLiteral("function")
            ).toInt();

        const int rawAddress =
            var.value(
                QStringLiteral("address")
            ).toInt();


        QString modbusAddress;


        if (
            function == 1
            ||
            function == 5
            ||
            function == 15
        )
        {
            modbusAddress =
                QStringLiteral("%1")
                .arg(
                    rawAddress + 1,
                    5,
                    10,
                    QChar('0')
                );
        }
        else if (
            function == 2
        )
        {
            modbusAddress =
                QString::number(
                    10001
                    +
                    rawAddress
                );
        }
        else if (
            function == 3
            ||
            function == 6
            ||
            function == 16
        )
        {
            modbusAddress =
                QString::number(
                    40001
                    +
                    rawAddress
                );
        }
        else if (
            function == 4
        )
        {
            modbusAddress =
                QString::number(
                    30001
                    +
                    rawAddress
                );
        }
        else
        {
            modbusAddress =
                QString::number(
                    rawAddress
                );
        }


        onlineTable_->setItem(
            tableRow,
            3,
            new QTableWidgetItem(
                modbusAddress
            )
        );


        // 读写方向

        const QString direction =
            var.value(
                QStringLiteral("direction")
            ).toString();


        onlineTable_->setItem(
            tableRow,
            4,
            new QTableWidgetItem(
                direction
            )
        );


        // 数据类型

        const QString datatype =
            var.value(
                QStringLiteral("datatype")
            ).toString();


        onlineTable_->setItem(
            tableRow,
            5,
            new QTableWidgetItem(
                datatype
            )
        );


        // 大小端转换

        const QString byteOrder =
            var.value(
                QStringLiteral("byteOrder")
            ).toString();


        onlineTable_->setItem(
            tableRow,
            6,
            new QTableWidgetItem(
                byteOrder
            )
        );


        // 实时值

        const double currentValue =
            var.value(
                QStringLiteral("value")
            ).toDouble();


        QString valueText;


        if (
            datatype.compare(
                QStringLiteral("UINT16"),
                Qt::CaseInsensitive
            ) == 0
            ||
            datatype.compare(
                QStringLiteral("INT16"),
                Qt::CaseInsensitive
            ) == 0
            ||
            datatype.compare(
                QStringLiteral("UINT32"),
                Qt::CaseInsensitive
            ) == 0
            ||
            datatype.compare(
                QStringLiteral("INT32"),
                Qt::CaseInsensitive
            ) == 0
        )
        {
            valueText =
                QString::number(
                    currentValue,
                    'f',
                    0
                );
        }
        else
        {
            valueText =
                QString::number(
                    currentValue,
                    'f',
                    6
                );
        }


        auto *valueItem =
            new QTableWidgetItem(
                valueText
            );


        valueItem->setTextAlignment(
            Qt::AlignRight
            |
            Qt::AlignVCenter
        );


        onlineTable_->setItem(
            tableRow,
            7,
            valueItem
        );


        // 质量
        //
        // status:
        //     1 = Modbus 通信正常
        //     0 = Modbus 通信异常
        //
        // forced:
        //     true  = 当前逻辑点被强制
        //     false = 正常实时值
        //
        // 显示优先级：
        //     FORCED > GOOD / BAD

        const int status =
            var.value(
                QStringLiteral("status")
            ).toInt();


        const bool forced =
            var.value(
                QStringLiteral("forced")
            ).toBool();

        // 保存当前逻辑点的强制状态
        //
        // Qt::UserRole + 2:
        //     false = 正常
        //     true  = 强制
        //
        // 右键菜单通过这个状态决定是否显示“解除强制”。

        if (
            QTableWidgetItem *idItem =
                onlineTable_->item(
                    tableRow,
                    0
                )
        )
        {
            idItem->setData(
                Qt::UserRole + 2,
                forced
            );
        }

        QString quality;

        if (forced)
        {
            quality =
                QStringLiteral("FORCED");
        }
        else if (
            status == 1
        )
        {
            quality =
                QStringLiteral("GOOD");
        }
        else
        {
            quality =
                QStringLiteral("BAD");
        }


        auto *qualityItem =
            new QTableWidgetItem(
                quality
            );


        qualityItem->setTextAlignment(
            Qt::AlignCenter
        );


        QFont qualityFont =
            qualityItem->font();

        qualityFont.setBold(
            true
        );

        qualityItem->setFont(
            qualityFont
        );


        // 质量颜色
        //
        // FORCED = 橙色
        // GOOD   = 绿色
        // BAD    = 红色

        if (forced)
        {
            qualityItem->setForeground(
                QBrush(
                    QColor("#d07000")
                )
            );
        }
        else if (
            status == 1
        )
        {
            qualityItem->setForeground(
                QBrush(
                    QColor("#008000")
                )
            );
        }
        else
        {
            qualityItem->setForeground(
                QBrush(
                    QColor("#c00000")
                )
            );
        }


        onlineTable_->setItem(
            tableRow,
            8,
            qualityItem
        );


        // 采集时间

        const QString timestamp =
            var.value(
                QStringLiteral("timestamp")
            ).toString();


        onlineTable_->setItem(
            tableRow,
            9,
            new QTableWidgetItem(
                timestamp
            )
        );
    }
}


    // 状态栏

    statusBar()->showMessage(
        QStringLiteral(
            "Modbus 采集服务：已读取 %1 个变量"
        )
        .arg(
            variables.size()
        )
    );
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
