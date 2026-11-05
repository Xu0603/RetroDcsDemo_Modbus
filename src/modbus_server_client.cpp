#include "modbus_server_client.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtNetwork/QTcpSocket>

namespace {

constexpr int kHeaderSize = 16;
constexpr quint32 kMaxPayloadSize = 1024 * 1024;
constexpr int kConnectTimeoutMs = 1000;
constexpr int kReadTimeoutMs = 1000;

bool readUntil(QTcpSocket &socket, QByteArray &data, int expectedSize, int timeoutMs)
{
    while (data.size() < expectedSize) {
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
        data += socket.readAll();
    }
    return true;
}

} // namespace

ModbusServerClient::ModbusServerClient(const QString &host, quint16 port)
    : host_(host), port_(port)
{
}

QJsonObject ModbusServerClient::request(quint16 messageType, quint32 &requestId,
                                        QString *error, const QJsonObject &payloadObject) const
{
    if (error)
        error->clear();

    QTcpSocket socket;
    socket.connectToHost(host_, port_);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("无法连接 Modbus 采集服务 %1:%2：%3")
                         .arg(host_).arg(port_).arg(socket.errorString());
        }
        return {};
    }

    const QByteArray payload = QJsonDocument(payloadObject).toJson(QJsonDocument::Compact);
    const quint32 sentRequestId = ++requestId;
    QByteArray frame;
    QDataStream writer(&frame, QIODevice::WriteOnly);
    // PYSM 的整数统一使用网络字节序，头部固定为 magic/version/type/id/length。
    writer.setByteOrder(QDataStream::BigEndian);
    writer.writeRawData("PYSM", 4);
    writer << quint16(1) << messageType << sentRequestId << quint32(payload.size());
    frame.append(payload);

    if (socket.write(frame) != frame.size()) {
        if (error)
            *error = QStringLiteral("发送 Modbus 采集服务请求失败");
        return {};
    }
    if (!socket.waitForBytesWritten(kConnectTimeoutMs)) {
        if (error)
            *error = QStringLiteral("发送 Modbus 采集服务请求超时：%1").arg(socket.errorString());
        return {};
    }

    QByteArray response;
    if (!readUntil(socket, response, kHeaderSize, kReadTimeoutMs)) {
        if (error)
            *error = response.isEmpty() ? QStringLiteral("Modbus 采集服务响应超时")
                                        : QStringLiteral("Modbus 采集服务响应头不完整");
        return {};
    }
    if (response.left(4) != QByteArrayLiteral("PYSM")) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务响应 Magic 无效");
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

    if (version != 1) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务协议版本不支持：%1").arg(version);
        return {};
    }

    // 服务端通过置高位区分响应和请求，例如 0x0002 对应 0x8002。
    const quint16 expectedType = static_cast<quint16>(messageType | 0x8000);
    if (responseType != expectedType) {
        if (error) {
            *error = QStringLiteral("Modbus 采集服务响应类型错误：收到 0x%1，期望 0x%2")
                         .arg(QString::number(responseType, 16).toUpper())
                         .arg(QString::number(expectedType, 16).toUpper());
        }
        return {};
    }
    if (responseId != sentRequestId) {
        if (error) {
            *error = QStringLiteral("Modbus 采集服务 Request ID 不匹配：收到 %1，期望 %2")
                         .arg(responseId).arg(sentRequestId);
        }
        return {};
    }
    // 长度来自对端，先限制上限再转换为 int，避免异常帧造成大内存分配或溢出。
    if (payloadLength > kMaxPayloadSize) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务 Payload 过大：%1 字节").arg(payloadLength);
        return {};
    }

    const int totalSize = kHeaderSize + static_cast<int>(payloadLength);
    if (!readUntil(socket, response, totalSize, kReadTimeoutMs)) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务 Payload 接收超时");
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        response.mid(kHeaderSize, static_cast<int>(payloadLength)), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务 JSON 解析失败：%1").arg(parseError.errorString());
        return {};
    }
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Modbus 采集服务返回的 JSON 不是 Object");
        return {};
    }
    return document.object();
}
