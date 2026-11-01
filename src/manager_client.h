#pragma once

#include <QJsonObject>
#include <QString>

class ManagerClient
{
public:
    explicit ManagerClient(quint16 port = 19091);
    QJsonObject request(quint16 messageType, quint32 &requestId, QString *error = nullptr,
                        const QJsonObject &payload = QJsonObject()) const;

private:
    quint16 port_;
};
