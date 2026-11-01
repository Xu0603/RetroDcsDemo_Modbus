#pragma once

#include <QJsonObject>
#include <QString>

class ModbusServerClient
{
public:

    // ModbusCollector 的 ModbusServer 默认监听：
    // 127.0.0.1:19091
    explicit ModbusServerClient(
        const QString &host = QStringLiteral("127.0.0.1"),
        quint16 port = 19091
    );

    // =========================================================
    // 请求 ModbusServer
    //
    // messageType:
    //
    // 1 = PING
    // 2 = SNAPSHOT
    //
    // requestId:
    //   由客户端自动递增
    //
    // error:
    //   输出错误信息
    //
    // payload:
    //   请求 JSON
    //
    // 返回：
    //   ModbusServer 返回的 JSON Object
    // =========================================================

    QJsonObject request(
        quint16 messageType,
        quint32 &requestId,
        QString *error = nullptr,
        const QJsonObject &payload = QJsonObject()
    ) const;


private:

    QString host_;

    quint16 port_;
};
