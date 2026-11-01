#include "modbus_server_client.h"

#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>


// ============================================================
// ModbusServerClient
//
// 用于连接 ModbusCollector 的 ModbusServer：
//
//     127.0.0.1:19091
//
// 协议：PYSM
//
// 请求格式：
//
//     4 bytes   magic       "PYSM"
//     2 bytes   version     1
//     2 bytes   messageType
//     4 bytes   requestId
//     4 bytes   payloadLength
//     N bytes   JSON payload
//
// 所有整数均为 BigEndian。
// ============================================================


ModbusServerClient::ModbusServerClient(
    const QString &host,
    quint16 port
)
    : host_(host),
      port_(port)
{
}


// ============================================================
// request
// ============================================================

QJsonObject ModbusServerClient::request(
    quint16 messageType,
    quint32 &requestId,
    QString *error,
    const QJsonObject &payloadObject
) const
{
    if (error)
    {
        error->clear();
    }


    // ========================================================
    // 1. 创建 TCP Socket
    // ========================================================

    QTcpSocket socket;


    // ========================================================
    // 2. 连接 ModbusServer
    // ========================================================

    socket.connectToHost(
        host_,
        port_
    );


    if (!socket.waitForConnected(1000))
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "无法连接 ModbusServer %1:%2：%3"
                )
                .arg(host_)
                .arg(port_)
                .arg(socket.errorString());
        }

        return {};
    }


    // ========================================================
    // 3. 生成 JSON Payload
    // ========================================================

    const QByteArray payload =
        QJsonDocument(
            payloadObject
        ).toJson(
            QJsonDocument::Compact
        );


    // ========================================================
    // 4. 创建 PYSM 请求帧
    // ========================================================

    QByteArray frame;


    QDataStream writer(
        &frame,
        QIODevice::WriteOnly
    );


    writer.setByteOrder(
        QDataStream::BigEndian
    );


    // --------------------------------------------------------
    // request ID 自动递增
    // --------------------------------------------------------

    const quint32 sentRequestId =
        ++requestId;


    // --------------------------------------------------------
    // PYSM Header
    // --------------------------------------------------------

    writer.writeRawData(
        "PYSM",
        4
    );


    writer
        << quint16(1)
        << messageType
        << sentRequestId
        << quint32(payload.size());


    // --------------------------------------------------------
    // JSON payload
    // --------------------------------------------------------

    frame.append(
        payload
    );


    // ========================================================
    // 5. 发送请求
    // ========================================================

    const qint64 written =
        socket.write(frame);


    if (written != frame.size())
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "发送 ModbusServer 请求失败"
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    if (!socket.waitForBytesWritten(1000))
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "发送 ModbusServer 请求超时：%1"
                )
                .arg(socket.errorString());
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 6. 等待响应
    // ========================================================

    QByteArray response;


    // PYSM Header 固定 16 字节
    constexpr int HEADER_SIZE = 16;


    if (!socket.waitForReadyRead(1000))
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer 响应超时"
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    response +=
        socket.readAll();


    // ========================================================
    // 7. 先保证 Header 收完整
    // ========================================================

    while (
        response.size() < HEADER_SIZE
    )
    {
        if (!socket.waitForReadyRead(500))
        {
            if (error)
            {
                *error =
                    QStringLiteral(
                        "ModbusServer 响应头不完整"
                    );
            }

            socket.disconnectFromHost();

            return {};
        }


        response +=
            socket.readAll();
    }


    // ========================================================
    // 8. 检查 Magic
    // ========================================================

    if (
        response.left(4)
        != QByteArray("PYSM", 4)
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer 响应 Magic 无效"
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 9. 解析 Header
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
    // 10. 检查版本
    // ========================================================

    if (version != 1)
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer 协议版本不支持：%1"
                )
                .arg(version);
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 11. 检查 Response Type
    //
    // 请求：
    //     1 -> 0x8001
    //     2 -> 0x8002
    //
    // Server 会给 messageType 加 0x8000
    // ========================================================

    const quint16 expectedResponseType =
        static_cast<quint16>(
            messageType | 0x8000
        );


    if (
        responseType
        != expectedResponseType
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer 响应类型错误：收到 0x%1，期望 0x%2"
                )
                .arg(
                    QString::number(
                        responseType,
                        16
                    ).toUpper()
                )
                .arg(
                    QString::number(
                        expectedResponseType,
                        16
                    ).toUpper()
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 12. 检查 Request ID
    // ========================================================

    if (
        responseId
        != sentRequestId
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer Request ID 不匹配：收到 %1，期望 %2"
                )
                .arg(responseId)
                .arg(sentRequestId);
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 13. 防止异常超大 Payload
    // ========================================================

    constexpr quint32 MAX_PAYLOAD_SIZE =
        1024 * 1024;


    if (
        payloadLength
        > MAX_PAYLOAD_SIZE
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer Payload 过大：%1 字节"
                )
                .arg(payloadLength);
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 14. 继续读取完整 Payload
    // ========================================================

    const int totalSize =
        HEADER_SIZE
        + static_cast<int>(
            payloadLength
        );


    while (
        response.size()
        < totalSize
    )
    {
        if (
            !socket.waitForReadyRead(1000)
        )
        {
            if (error)
            {
                *error =
                    QStringLiteral(
                        "ModbusServer Payload 接收超时"
                    );
            }

            socket.disconnectFromHost();

            return {};
        }


        response +=
            socket.readAll();
    }


    // ========================================================
    // 15. 提取 JSON
    // ========================================================

    const QByteArray jsonBytes =
        response.mid(
            HEADER_SIZE,
            static_cast<int>(
                payloadLength
            )
        );


    // ========================================================
    // 16. 解析 JSON
    // ========================================================

    QJsonParseError parseError;


    const QJsonDocument document =
        QJsonDocument::fromJson(
            jsonBytes,
            &parseError
        );


    if (
        parseError.error
        != QJsonParseError::NoError
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer JSON 解析失败：%1"
                )
                .arg(
                    parseError.errorString()
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    if (
        !document.isObject()
    )
    {
        if (error)
        {
            *error =
                QStringLiteral(
                    "ModbusServer 返回的 JSON 不是 Object"
                );
        }

        socket.disconnectFromHost();

        return {};
    }


    // ========================================================
    // 17. 返回 JSON Object
    // ========================================================

    socket.disconnectFromHost();


    return document.object();
}
