#ifndef FILETRANSFERCONTROLCODEC_H
#define FILETRANSFERCONTROLCODEC_H

#include<QByteArray>
#include"protocol.h"

namespace Protocol{

class FileTransferControlCodec
{
public:
    FileTransferControlCodec()=delete;

    /**
     * @brief 编码客户端文件传输开始请求。
     *
     * @param receiverId 目标客户端当前连接 ID。
     * @param transferId 本次文件传输的唯一标识。
     * @param fileName 文件名。
     * @param fileSize 文件总字节数。
     * @return 编码后的 JSON 字节数据。
     */
    static QByteArray encodeStartRequest(
        quint32 receiverId,const QString &transferId,
        const QString &fileName,qint64 fileSize);

    /**
     * @brief 解码客户端文件传输开始请求。
     *
     * @param data JSON 字节数据。
     * @param receiverId 用于接收目标客户端当前连接 ID。
     * @param transferId 用于接收本次文件传输标识。
     * @param fileName 用于接收文件名。
     * @param fileSize 用于接收文件总字节数。
     * @return 解码成功返回 true，否则返回 false。
     */
    static bool decodeStartRequest(
        const QByteArray&data,quint32 &receiverId,
        QString &transferId,QString &fileName,qint64 &fileSize);

    /**
     * @brief 编码服务器下发的文件传输开始消息。
     *
     * @param senderId 发送方客户端当前连接 ID。
     * @param transferId 本次文件传输的唯一标识。
     * @param fileName 文件名。
     * @param fileSize 文件总字节数。
     * @return 编码后的 JSON 字节数据。
     */
    static QByteArray encodeStart(
        quint32 senderId,const QString &transferId,
        const QString &fileName,qint64 fileSize);

    /**
     * @brief 解码服务器下发的文件传输开始消息。
     *
     * @param data JSON 字节数据。
     * @param senderId 用于接收发送方客户端当前连接 ID。
     * @param transferId 用于接收本次文件传输标识。
     * @param fileName 用于接收文件名。
     * @param fileSize 用于接收文件总字节数。
     * @return 解码成功返回 true，否则返回 false。
     */
    static bool decodeStart(
        const QByteArray&data,quint32 &senderId,
        QString &transferId,QString &fileName,qint64 &fileSize);

    /**
     * @brief 编码 FileEndRequest 和 FileEnd 共用的消息体。
     *
     * @param transferId 本次文件传输的唯一标识。
     * @return 编码后的 JSON 字节数据。
     */
    static QByteArray encodeEnd(const QString &transferId);

    /**
     * @brief 解码 FileEndRequest 和 FileEnd 共用的消息体。
     *
     * @param data JSON 字节数据。
     * @param transferId 用于接收本次文件传输标识，失败时保持原值。
     * @return 解码成功返回 true，否则返回 false。
     */
    static bool decodeEnd(const QByteArray &data,QString &transferId);

    /**
    * @brief 编码 FileAbortRequest 和 FileAbort 共用的消息体。
    *
    * @param transferId 本次文件传输的唯一标识。
    * @param reason 文件传输终止原因。
    * @return 编码后的 JSON 字节数据；参数非法时返回空 QByteArray。
    */
    static QByteArray encodeAbort(const QString&transferId,FileAbortReason reason);

    /**
    * @brief 解码 FileAbortRequest 和 FileAbort 共用的消息体。
    *
    * @param data JSON 字节数据。
    * @param transferId 用于接收文件传输标识，失败时保持原值。
    * @param reason 用于接收文件传输终止原因，失败时保持原值。
    * @return 解码成功返回 true，否则返回 false。
    */
    static bool decodeAbort(const QByteArray&data,
                            QString&transferId,FileAbortReason&reason);

    /**
    * @brief 编码文件接收完成确认。
    * @param transferId 文件传输唯一标识。
    * @return 编码后的 JSON 数据，参数非法时返回空 QByteArray。
    */
    static QByteArray encodeComplete(const QString &transferId);

    /**
    * @brief 解码文件接收完成确认。
    * @param data 编码后的 JSON 数据。
    * @param transferId 成功时输出规范化后的文件传输 ID。
    * @return 解码成功返回 true，否则返回 false。
    */
    static bool decodeComplete(const QByteArray &data,QString &transferId);
};

};
#endif // FILETRANSFERCONTROLCODEC_H
