#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QObject>
// #include <QTcpSocket>
#include <QByteArray>
#include<QList>
#include"protocol.h"
#include <QElapsedTimer>

class QTimer;
class QTcpSocket; //前置声明，.cpp里再写↑，减少头文件之间的耦合和不必要的编译依赖
namespace Protocol{
struct Packet;
}

/**
 * @brief 管理客户端与服务器之间的 TCP 连接及协议消息收发。
 *
 * 负责 QTcpSocket 生命周期、连接状态管理、
 * TCP 接收缓冲区维护及 QtLink 数据包解析，
 * 并通过信号向上层通知文本消息、在线客户端列表和网络错误。
 *
 * 当前类不负责：
 * - UI 展示
 * - 本地数据库持久化
 * - 自动重连
 * - 文件传输
 */
class ConnectionManager : public QObject
{
    Q_OBJECT
public:
    explicit ConnectionManager(QObject *parent = nullptr);

    /**
     * @brief 发起到服务器的异步连接。
     * @param host 服务器主机地址，可以是 IP、localhost 或域名。
     * @param port 服务器端口。
     */
    void connectToServer(const QString &host,quint16 port);

    /**
     * @brief 主动断开当前服务器连接。
     */
    void disconnectFromServer();

    // -----------------------------------------------------------------

    /**
    * @brief 向指定在线客户端发送文本消息请求。
    *
    * @param receiverId 目标客户端当前连接 ID。
    * @param text 文本消息内容。
    * @return 请求成功写入 Socket 发送缓冲区返回 true，否则返回 false。
    */
    bool sendTextMessage(quint32 receiverId,const QString &text);

    /**
     * @brief 向服务器发送文件传输开始请求。
     * @param receiverId 目标客户端连接 ID。
     * @param transferId 本次文件传输的唯一标识。
     * @param fileName 文件名。
     * @param fileSize 文件总字节数。
     * @return 成功写入 Socket 发送缓冲区返回 true，否则返回 false。
     */
    bool sendFileStartRequest(quint32 receiverId,const QString&transferId,
            const QString&fileName,qint64 fileSize);

    /**
    * @brief 向服务器发送一个文件数据块。
    * @param transferId 本次文件传输唯一标识。
    * @param offset 当前数据块在文件中的起始偏移。
    * @param chunkData 文件数据块。
    * @return 成功写入 Socket 发送缓冲区返回 true，否则返回 false。
    */
    bool sendFileChunkRequest(const QString&transferId,qint64 offset,
                              const QByteArray &chunkData);

    /**
    * @brief 获取当前 Socket 写缓冲区中等待发送的字节数。
    * @return 当前待发送字节数。
    */
    qint64 pendingBytesToWrite()const;

    /**
    * @brief 向服务器发送文件传输结束请求。
    * @param transferId 本次文件传输唯一标识。
    * @return 成功写入 Socket 发送缓冲区返回 true，否则返回 false。
    */
    bool sendFileEndRequest(const QString&transferId);

    /**
    * @brief 向服务端发送文件传输终止请求。
    *
    * @param transferId 文件传输唯一标识。
    * @param reason 文件传输终止原因。
    * @return 消息成功提交到本地 Socket 写缓冲时返回 true，否则返回 false。
    */
    bool sendFileAbortRequest(const QString &transferId,Protocol::FileAbortReason reason);

    /**
    * @brief 向服务端发送文件接收完成确认。
    * @param transferId 文件传输唯一标识。
    * @return 消息成功提交到本地 Socket 写缓冲时返回 true，否则返回 false。
    */
    bool sendFileCompleteRequest(const QString &transferId);

signals:
    /**
     * @brief TCP 连接建立成功。
     */
    void connected();

    /**
     * @brief TCP 连接已经断开。
     */
    void disconnected();

    /**
    * @brief 通知上层连接建立或连接过程发生错误。
    *
    * @param message 错误描述。
    */
    void connectionError(const QString &message);

    /**
    * @brief 通知上层发生非连接类运行错误。
    *
    * @param message 错误描述。
    */
    void operationError(const QString &message);

// ----------------------------------------------------------

    /**
    * @brief 通知上层收到一条服务器下发的文本消息。
    *
    * @param senderId 消息发送者的连接 ID。
    * @param text 文本消息内容。
    */
    void textMessageReceived(const quint32 senderId,const QString &text);

    /**
    * @brief 通知上层服务器下发了最新的在线客户端列表。
    *
    * @param selfId 当前客户端自身的连接 ID。
    * @param clientIds 当前所有在线客户端的连接 ID。
    */
    void onlineClientListReceived(quint32 selfId,const QList<quint32>&clientIds);

    /**
     * @brief 收到服务器转发的文件传输开始消息。
     * @param senderId 文件发送客户端 ID。
     * @param transferId 本次文件传输唯一标识。
     * @param fileName 文件名。
     * @param fileSize 文件总字节数。
     */
    void fileStartReceived(
        quint32 senderId,const QString&transferId,
        const QString&fileName,qint64 fileSize);

    /**
    * @brief 收到服务器转发的文件数据块。
    * @param transferId 本次文件传输唯一标识。
    * @param offset 当前数据块在文件中的起始偏移。
    * @param chunkData 文件数据块。
    */
    void fileChunkReceived(const QString &transferId,qint64 offset,
        const QByteArray &chunkData);

    /**
    * @brief Socket 有数据从写缓冲区发送出去。
    * @param bytes 本次写出的字节数。
    */
    void socketBytesWritten(qint64 bytes);

    /**
    * @brief 收到服务器转发的文件传输结束消息。
    * @param transferId 本次文件传输唯一标识。
    */
    void fileEndReceived(const QString&transferId);

    /**
    * @brief 收到服务端转发的文件传输终止通知。
    *
    * @param transferId 被终止的文件传输唯一标识。
    * @param reason 文件传输终止原因。
    */
    void fileAbortReceived(const QString &transferId,Protocol::FileAbortReason reason);

    /**
    * @brief 收到服务端下发的文件传输完成通知。
    * @param transferId 已完成的文件传输唯一标识。
    */
    void fileCompleteReceived(const QString &transferId);


private slots:
    /**
     * @brief 处理套接字可读事件。
     *
     * 读取当前已到达的 TCP 字节数据，并追加到接收缓冲区，
     * 后续再从缓冲区中按 QtLink 协议解析完整数据包。
     */
    void handleReadyRead();

    /**
     * @brief 处理 TCP 连接断开事件。
     *
     * 清理当前连接遗留的接收缓冲区，并通知上层连接已断开。
     */
    void handleSocketDisconnected();

    /**
    * @brief 处理 TCP 连接建立事件并启动心跳检测。
    */
    void handleSocketConnected();

    /**
    * @brief 周期检查服务器活动状态并发送心跳请求。
    */
    void handleHeartbeatTimer();

private:
    /**
    * @brief 将一个 QtLink 数据包写入 Socket 发送缓冲区。
    *
    * @param packet 已构造完成的 QtLink 数据包。
    * @return 成功写入 Socket 发送缓冲区返回 true，否则返回 false。
    */
    bool writePacket(const Protocol::Packet &packet);

    /**
    * @brief 向服务器发送空消息体的心跳请求。
    * @return 成功写入 Socket 发送缓冲区返回 true，否则返回 false。
    */
    bool sendHeartbeatRequest();

    QTcpSocket*m_socket;
    QByteArray m_receiveBuffer;

    QTimer*m_heartbeatTimer;
    QElapsedTimer m_connectionClock;
    qint64 m_lastServerActivityMs{0};

    static constexpr int HEARTBEAT_INTERVAL_MS=15*1000;
    static constexpr qint64 SERVER_TIMEOUT_MS=90*1000;


};

#endif // CONNECTIONMANAGER_H
