#pragma once

#include <QJsonObject>
#include <QString>

// ModbusCollector 采集服务的同步客户端。默认端口为 19091。
class ModbusServerClient
{
public:
    explicit ModbusServerClient(const QString &host = QStringLiteral("127.0.0.1"),
                                quint16 port = 19091);

    // messageType 由 PYSM 协议定义；requestId 在成功组帧时递增。
    QJsonObject request(quint16 messageType, quint32 &requestId, QString *error = nullptr,
                        const QJsonObject &payload = QJsonObject()) const;

private:
    QString host_;
    quint16 port_;
};
