#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
#include<QHash>
#include<QByteArray>
class QTcpServer;
class QTcpSocket;
/**
 * @brief QtLink 服务端 TCP 连接与消息转发管理类。
 *
 * 负责监听 TCP 端口、接受和维护客户端连接，
 * 管理各客户端连接会话及接收缓冲区，
 * 解析 QtLink 数据包并完成基础消息转发，
 * 同时通过信号向 UI 层通知服务器状态、在线人数和运行日志。
 */
class TcpServer : public QObject
{
    Q_OBJECT
public:
    explicit TcpServer(QObject *parent = nullptr);

    /**
     * @brief 启动 TCP 服务器。
     * @param port 要监听的端口。
     * @return 启动成功返回 true，否则返回 false。
     */
    bool startServer(quint16 port);

    /**
     * @brief 停止服务器并断开现有客户端。
     */
    void stopServer();

private:
    /**
    * @brief 向所有已连接客户端发送最新的在线客户端列表。
    */
    void broadcastOnlineClientList();

    /**
     * @brief 处理客户端发来的文件传输开始请求。
     * @param senderId 发起文件传输的客户端 ID。
     * @param body FileStartRequest 消息体。
     */
    void handleFileStartRequest(quint32 senderId,const QByteArray &body);

    /**
    * @brief 处理客户端发送的文本消息请求。
    * @param senderId 消息发送客户端 ID。
    * @param body TextSendRequest 消息体。
    */
    void handleTextSendRequest(quint32 senderId,const QByteArray&body);

    /**
    * @brief 处理客户端发送的文件数据块请求。
    * @param senderId 文件发送客户端 ID。
    * @param body FileChunkRequest 消息体。
    */
    void handleFileChunkRequest(quint32 senderId,const QByteArray&body);

    /**
    * @brief 处理客户端发送的文件传输结束请求。
    * @param senderId 文件发送客户端 ID。
    * @param body FileEndRequest 消息体。
    */
    void handleFileEndRequest(quint32 senderId,const QByteArray&body);

    /**
    * @brief 清理所有与指定客户端相关的活动文件传输。
    * @param clientId 已断开连接的客户端 ID。
   */
    void cleanupTransfersForClient(quint32 clientId);

    /**
     * @brief 查找导致指定发送客户端需要暂停读取的拥塞接收端。
     * @param senderId 文件发送客户端 ID。
     * @param receiverId 用于接收发生拥塞的接收客户端 ID。
     * @return 找到拥塞接收端返回 true，否则返回 false。
     */
    bool findBackpressuredReceiver(
        quint32 senderId,quint32 &receiverId) const;

    /**
    * @brief 接收端写缓冲恢复后，尝试继续处理向其发送文件的客户端。
    * @param receiverId 文件接收客户端 ID。
    */
    void resumeSendersForReceiver(quint32 receiverId);

signals:
    void clientCountChanged(int count);

    void logMessage(const QString &message);

    void serverError(const QString &message);

private slots:
    /**
    * @brief 处理新的客户端连接。
    *
    * 获取 QTcpServer 中等待处理的客户端连接，
    * 保存客户端套接字并建立数据接收、断开等信号连接。
    */
       void handleNewConnection();

    /**
    * @brief 处理指定客户端的可读事件。
    *
    * 根据客户端连接 ID 查找对应会话，
    * 将当前到达的 TCP 字节追加到其独立接收缓冲区，
    * 并从缓冲区中解析完整的 QtLink 数据包。
    *
    * @param clientId 产生可读数据的客户端连接 ID。
    */
    void handleClientReadyRead(quint32 clientId);

private:
    static constexpr qint64 FORWARD_HIGH_WATER_MARK=
        256*1024;

    static constexpr qint64 FORWARD_LOW_WATER_MARK=
        128*1024;

    static constexpr qint64 SOCKET_READ_BUFFER_LIMIT=
        256*1024;

    struct ClientSession{
        QTcpSocket *socket{nullptr};
        QByteArray receiveBuffer;

        bool readPaused{false};
        quint32 blockedReceiverId{0};
    };
    struct ActiveTransfer{
        quint32 receiverId{0};
        quint32 senderId{0};
        qint64 fileSize{0};
        qint64 nextOffset{0};
    };

    QTcpServer *m_server;
    QHash<quint32,ClientSession>m_sessions;
    quint32 m_nextClientId{1};

    QHash<QString,ActiveTransfer>m_activeTransfers;
};

#endif // TCPSERVER_H
