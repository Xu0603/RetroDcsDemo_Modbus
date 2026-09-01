#include "modbus_shared_memory_reader.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <pthread.h>

#include <cstring>


// ============================================================
// 必须和 ModbusCollector 的 shared_memory.h 完全一致
// ============================================================

#define MAX_VARIABLES 20


// ============================================================
// 必须和 ModbusCollector 的 Variable 完全一致
// ============================================================

struct Variable
{
    char name[32];

    int slave;

    int function;

    int address;

    char datatype[16];

    double value;

    char timestamp[32];

    char telegram[256];

    int status;
};


// ============================================================
// 必须和 ModbusCollector 的 SharedData 完全一致
// ============================================================

struct SharedData
{
    pthread_mutex_t mutex;

    int count;

    Variable vars[MAX_VARIABLES];
};


// ============================================================
// ModbusCollector 使用的共享内存名称
//
// 对应：
// #define SHM_NAME "/modbus_data"
// ============================================================

static constexpr const char *SHM_NAME = "/modbus_data";


// ============================================================
// 读取 Modbus 共享内存
// ============================================================

bool ModbusSharedMemoryReader::read(
    QVector<ModbusSharedVariable> &variables,
    QString *error
)
{
    // --------------------------------------------------------
    // 清空上一次读取的数据
    // --------------------------------------------------------

    variables.clear();

    if (error)
        error->clear();


    // --------------------------------------------------------
    // 打开 ModbusCollector 创建的共享内存
    //
    // 注意：
    // 不使用 O_CREAT
    //
    // 如果 ModbusCollector 没启动，
    // 这里应该直接失败。
    // --------------------------------------------------------

    int fd = shm_open(
    SHM_NAME,
    O_RDWR,
    0666
);


    if (fd < 0)
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法打开 Modbus 共享内存 %1，请确认 ModbusCollector 已启动"
                ).arg(QString::fromLatin1(SHM_NAME));
        }

        return false;
    }


    // --------------------------------------------------------
    // 获取共享内存大小
    // --------------------------------------------------------

    struct stat st;

    if (fstat(fd, &st) < 0)
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法获取 Modbus 共享内存的信息"
                );
        }

        close(fd);

        return false;
    }


    // --------------------------------------------------------
    // 检查共享内存大小
    //
    // 如果 Qt 和 ModbusCollector 两边结构体不一致，
    // 这里可以提前发现问题。
    // --------------------------------------------------------

    if (
        st.st_size <
        static_cast<off_t>(sizeof(SharedData))
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "Modbus 共享内存大小不匹配：实际 %1 字节，期望至少 %2 字节"
                )
                .arg(st.st_size)
                .arg(sizeof(SharedData));
        }

        close(fd);

        return false;
    }


    // --------------------------------------------------------
    // 映射共享内存
    // --------------------------------------------------------

    void *address = mmap(
    nullptr,
    sizeof(SharedData),
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd,
    0
);


    close(fd);


    if (address == MAP_FAILED)
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法映射 Modbus 共享内存"
                );
        }

        return false;
    }


    // --------------------------------------------------------
    // 转换为 SharedData
    // --------------------------------------------------------

    SharedData *data =
        static_cast<SharedData *>(address);


    // --------------------------------------------------------
    // 锁共享内存
    //
    // ModbusCollector 写数据时也使用同一个 mutex。
    //
    // 这样可以避免：
    //
    // Collector 正在写
    // Qt 同时读取
    //
    // 导致读取到一半的数据。
    // --------------------------------------------------------

    if (
        pthread_mutex_lock(
            &data->mutex
        ) != 0
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法锁定 Modbus 共享内存"
                );
        }

        munmap(
            data,
            sizeof(SharedData)
        );

        return false;
    }


    // --------------------------------------------------------
    // 获取变量数量
    // --------------------------------------------------------

    int count =
        data->count;


    // --------------------------------------------------------
    // 安全检查
    // --------------------------------------------------------

    if (
        count < 0 ||
        count > MAX_VARIABLES
    )
    {
        count = 0;

        if (error)
        {
            *error =
                QStringLiteral(
                    "Modbus 共享内存中的变量数量异常"
                );
        }
    }


    // --------------------------------------------------------
    // 复制共享内存中的变量
    //
    // 注意：
    // 不要把共享内存中的指针直接交给 Qt。
    //
    // 这里全部复制成 Qt 自己的数据。
    // --------------------------------------------------------

    for (
        int i = 0;
        i < count;
        ++i
    )
    {
        const Variable &source =
            data->vars[i];


        ModbusSharedVariable variable;


        variable.name =
            QString::fromLocal8Bit(
                source.name
            );


        variable.slave =
            source.slave;


        variable.function =
            source.function;


        variable.address =
            source.address;


        variable.datatype =
            QString::fromLocal8Bit(
                source.datatype
            );


        variable.value =
            source.value;


        variable.timestamp =
            QString::fromLocal8Bit(
                source.timestamp
            );


        variable.telegram =
            QString::fromLocal8Bit(
                source.telegram
            );


        variable.status =
            source.status;


        variables.append(
            variable
        );
    }


    // --------------------------------------------------------
    // 解锁
    // --------------------------------------------------------

    pthread_mutex_unlock(
        &data->mutex
    );


    // --------------------------------------------------------
    // 解除映射
    // --------------------------------------------------------

    munmap(
        data,
        sizeof(SharedData)
    );


    return true;
}