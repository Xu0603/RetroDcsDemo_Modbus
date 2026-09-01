#include "manager_client.h"

#include <QtNetwork>

ManagerClient::ManagerClient(quint16 port) : port_(port) {}

QJsonObject ManagerClient::request(quint16 messageType, quint32 &requestId, QString *error,
                                   const QJsonObject &requestObject) const
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port_);
    if (!socket.waitForConnected(250)) {
        if (error) *error = socket.errorString();
        return {};
    }
    const QByteArray payload = QJsonDocument(requestObject).toJson(QJsonDocument::Compact);
    QByteArray frame;
    QDataStream writer(&frame, QIODevice::WriteOnly);
    writer.setByteOrder(QDataStream::BigEndian);
    const quint32 sentRequestId = ++requestId;
    writer.writeRawData("PYSM", 4);
    writer << quint16(1) << messageType << sentRequestId << quint32(payload.size());
    frame.append(payload);
    socket.write(frame);
    if (!socket.waitForBytesWritten(250) || !socket.waitForReadyRead(500)) {
        if (error) *error = QStringLiteral("system-managerd 响应超时");
        return {};
    }
    QByteArray response = socket.readAll();
    while (response.size() < 16 && socket.waitForReadyRead(100)) response += socket.readAll();
    if (response.size() < 16 || response.left(4) != QByteArray("PYSM", 4)) {
        if (error) *error = QStringLiteral("TCP 响应帧头无效");
        return {};
    }
    QDataStream reader(response);
    reader.setByteOrder(QDataStream::BigEndian);
    char magic[4]; quint16 version = 0, responseType = 0; quint32 responseId = 0, payloadLength = 0;
    reader.readRawData(magic, 4);
    reader >> version >> responseType >> responseId >> payloadLength;
    while (response.size() < 16 + int(payloadLength) && socket.waitForReadyRead(200)) response += socket.readAll();
    if (version != 1 || responseType != quint16(messageType | 0x8000) || responseId != sentRequestId ||
            payloadLength > 1024 * 1024 || response.size() < 16 + int(payloadLength)) {
        if (error) *error = QStringLiteral("TCP 响应长度或版本无效");
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(response.mid(16, payloadLength), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("JSON 数据无效: %1").arg(parseError.errorString());
        return {};
    }
    return document.object();
}
