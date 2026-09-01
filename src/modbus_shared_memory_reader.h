#pragma once

#include <QString>
#include <QVector>


// ============================================================
// Qt 内部使用的 Modbus 变量结构
//
// 这个结构对应 ModbusCollector 写入共享内存的数据。
// ============================================================

struct ModbusSharedVariable
{
    QString name;

    int slave = 0;

    int function = 0;

    int address = 0;

    QString datatype;

    double value = 0.0;

    QString timestamp;

    QString telegram;

    int status = 0;
};


// ============================================================
// Modbus 共享内存读取器
//
// 负责读取 ModbusCollector 创建的：
//
//     /modbus_data
//
// 注意：
// Qt 不创建共享内存。
// Qt 只是读取已经由 ModbusCollector 创建的数据。
// ============================================================

class ModbusSharedMemoryReader
{
public:

    // --------------------------------------------------------
    // 读取共享内存中的所有变量
    //
    // 返回：
    // true  = 读取成功
    // false = 读取失败
    //
    // variables:
    // 输出读取到的变量
    //
    // error:
    // 输出错误信息
    // --------------------------------------------------------

    bool read(
        QVector<ModbusSharedVariable> &variables,
        QString *error = nullptr
    );
};