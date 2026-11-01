#include "manager_client.h"

#include <QtNetwork>
#include <QDataStream>
#include <QJsonDocument>


// ============================================================
// 构造
// ============================================================

ManagerClient::ManagerClient(
    quint16 port
)
    : port_(port)
{
}


// ============================================================
// PYSM 请求
// ============================================================

QJsonObject ManagerClient::request(
    quint16 messageType,
    quint32 &requestId,
    QString *error,
    const QJsonObject &requestObject
) const
{
    if (error)
        error->clear();


    // ========================================================
    // 创建 TCP Socket
    // ========================================================

    QTcpSocket socket;


    socket.connectToHost(
        QHostAddress::LocalHost,
        port_
    );


    if (!socket.waitForConnected(1000))
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法连接 PYSM Server 127.0.0.1:%1：%2"
                )
                .arg(port_)
                .arg(socket.errorString());
        }

        return {};
    }


    // ========================================================
    // JSON Payload
    // ========================================================

    const QByteArray payload =
        QJsonDocument(
            requestObject
        ).toJson(
            QJsonDocument::Compact
        );


    // ========================================================
    // 构造 PYSM Frame
    //
    // Header:
    //
    // 4 bytes  magic
    // 2 bytes  version
    // 2 bytes  messageType
    // 4 bytes  requestId
    // 4 bytes  payloadLength
    //
    // 总计16 bytes
    // ========================================================

    QByteArray frame;


    QDataStream writer(
        &frame,
        QIODevice::WriteOnly
    );


    writer.setByteOrder(
        QDataStream::BigEndian
    );


    const quint32 sentRequestId =
        ++requestId;


    writer.writeRawData(
        "PYSM",
        4
    );


    writer
        << quint16(1)
        << messageType
        << sentRequestId
        << quint32(payload.size());


    frame.append(
        payload
    );


    // ========================================================
    // 发送
    // ========================================================

    if (
        socket.write(frame)
        != frame.size()
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "PYSM 请求发送失败"
                );
        }

        return {};
    }


    if (
        !socket.waitForBytesWritten(1000)
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "PYSM 请求发送超时"
                );
        }

        return {};
    }


    // ========================================================
    // 等待响应
    // ========================================================

    if (
        !socket.waitForReadyRead(1000)
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "PYSM Server 响应超时"
                );
        }

        return {};
    }


    // ========================================================
    // 接收响应
    // ========================================================

    QByteArray response =
        socket.readAll();


    while (
        response.size() < 16
        &&
        socket.waitForReadyRead(200)
    )
    {
        response +=
            socket.readAll();
    }


    // ========================================================
    // 检查 Header
    // ========================================================

    if (
        response.size() < 16
        ||
        response.left(4)
            != QByteArray("PYSM", 4)
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "TCP 响应 PYSM 帧头无效"
                );
        }

        return {};
    }


    // ========================================================
    // 解析 Header
    // ========================================================

    QDataStream reader(
        response
    );


    reader.setByteOrder(
        QDataStream::BigEndian
    );


    char magic[4];

    quint16 version = 0;

    quint16 responseType = 0;

    quint32 responseId = 0;

    quint32 payloadLength = 0;


    reader.readRawData(
        magic,
        4
    );


    reader
        >> version
        >> responseType
        >> responseId
        >> payloadLength;


    // ========================================================
    // 等待完整 Payload
    // ========================================================

    while (
        response.size()
            <
        16 + static_cast<int>(payloadLength)
        &&
        socket.waitForReadyRead(200)
    )
    {
        response +=
            socket.readAll();
    }


    // ========================================================
    // 校验响应
    // ========================================================

    if (
        version != 1
        ||
        responseType
            != quint16(messageType | 0x8000)
        ||
        responseId != sentRequestId
        ||
        payloadLength > 1024 * 1024
        ||
        response.size()
            <
            16 + static_cast<int>(payloadLength)
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "TCP 响应长度、版本或 requestId 无效"
                );
        }

        return {};
    }


    // ========================================================
    // 提取 JSON
    // ========================================================

    const QByteArray jsonData =
        response.mid(
            16,
            static_cast<int>(payloadLength)
        );


    // ========================================================
    // JSON 解析
    // ========================================================

    QJsonParseError parseError;


    const QJsonDocument document =
        QJsonDocument::fromJson(
            jsonData,
            &parseError
        );


    if (
        parseError.error
            !=
        QJsonParseError::NoError
        ||
        !document.isObject()
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "JSON 数据无效: %1"
                )
                .arg(
                    parseError.errorString()
                );
        }

        return {};
    }


    // ========================================================
    // 返回 JSON Object
    // ========================================================

    return document.object();
}
