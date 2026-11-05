#include "manager_client.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

namespace {

constexpr int kHeaderSize = 16;
constexpr quint32 kMaxPayloadSize = 1024 * 1024;

bool readUntil(QTcpSocket &socket, QByteArray &data, int size, int timeoutMs)
{
    while (data.size() < size) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
        data += socket.readAll();
    }
    return true;
}

} // namespace

ManagerClient::ManagerClient(quint16 port)
    : port_(port)
{
}

QJsonObject ManagerClient::request(quint16 messageType, quint32 &requestId, QString *error,
                                   const QJsonObject &requestObject) const
{
    if (error)
        error->clear();

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port_);
    if (!socket.waitForConnected(1000)) {
        if (error) {
            *error = QStringLiteral("无法连接 PYSM Server 127.0.0.1:%1：%2")
                         .arg(port_).arg(socket.errorString());
        }
        return {};
    }

    const QByteArray payload = QJsonDocument(requestObject).toJson(QJsonDocument::Compact);
    const quint32 sentRequestId = ++requestId;
    QByteArray frame;
    QDataStream writer(&frame, QIODevice::WriteOnly);
    // PYSM 头共 16 字节，所有数字均按大端序发送。
    writer.setByteOrder(QDataStream::BigEndian);
    writer.writeRawData("PYSM", 4);
    writer << quint16(1) << messageType << sentRequestId << quint32(payload.size());
    frame.append(payload);

    if (socket.write(frame) != frame.size()) {
        if (error)
            *error = QStringLiteral("PYSM 请求发送失败");
        return {};
    }
    if (!socket.waitForBytesWritten(1000)) {
        if (error)
            *error = QStringLiteral("PYSM 请求发送超时");
        return {};
    }

    QByteArray response;
    if (!readUntil(socket, response, kHeaderSize, 1000)
        || response.left(4) != QByteArrayLiteral("PYSM")) {
        if (error)
            *error = QStringLiteral("TCP 响应 PYSM 帧头无效");
        return {};
    }

    QDataStream reader(response);
    reader.setByteOrder(QDataStream::BigEndian);
    char magic[4];
    quint16 version = 0;
    quint16 responseType = 0;
    quint32 responseId = 0;
    quint32 payloadLength = 0;
    reader.readRawData(magic, 4);
    reader >> version >> responseType >> responseId >> payloadLength;

    // payloadLength 不可信，必须在转为 int 和继续读取前做上限检查。
    const quint16 expectedType = static_cast<quint16>(messageType | 0x8000);
    if (version != 1 || responseType != expectedType || responseId != sentRequestId
        || payloadLength > kMaxPayloadSize) {
        if (error)
            *error = QStringLiteral("TCP 响应长度、版本或 requestId 无效");
        return {};
    }

    const int totalSize = kHeaderSize + static_cast<int>(payloadLength);
    if (!readUntil(socket, response, totalSize, 200)) {
        if (error)
            *error = QStringLiteral("TCP 响应长度、版本或 requestId 无效");
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        response.mid(kHeaderSize, static_cast<int>(payloadLength)), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 数据无效: %1").arg(parseError.errorString());
        return {};
    }
    return document.object();
}
