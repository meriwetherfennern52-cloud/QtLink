#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
#include<QHash>
#include<QByteArray>
#include<QElapsedTimer>
#include"protocol.h"
class QTcpServer;
class QTcpSocket;
class QTimer;
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
    * @brief 处理文件接收端发来的最终完成确认。
    *
    * 验证请求客户端确实是指定传输的接收端，
    * 并确认传输处于等待接收完成阶段。
    * 验证成功后向发送端转发 FileComplete。
    *
    * @param receiverId 发出完成确认的客户端 ID。
    * @param body FileCompleteRequest 消息体。
    */
    void handleFileCompleteRequest(quint32 receiverId,const QByteArray &body);

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

    /**
    * @brief 删除传输后解除发送端不再需要的背压状态。
    * @param senderId 文件发送客户端 ID。
    * @param receiverId 原文件接收客户端 ID。
    */
    void resumeSenderAfterTransferRemoval(quint32 senderId,quint32 receiverId);

    /**
    * @brief 判断发送端是否仍有向指定接收端传输数据的任务。
    * @param senderId 文件发送客户端 ID。
    * @param receiverId 文件接收客户端 ID。
    * @return 仍存在数据传输阶段的任务时返回 true。
    */
    bool hasTransferringDependency(quint32 senderId,quint32 receiverId) const;

    /**
    * @brief 将指定客户端连接安排为确定断开。
    * @param clientId 要断开的客户端 ID。
    */
    void scheduleClientDisconnect(quint32 clientId);

    /**
    * @brief 刷新指定接收端相关文件传输的活动时间。
    * @param receiverId 文件接收客户端 ID。
    */
    void updateReceiverTransferActivity(quint32 receiverId);

    /**
    * @brief 检查并终止长时间没有进展的文件传输。
    */
    void checkStalledTransfers();

    /**
    * @brief 处理客户端发来的文件传输终止请求。
    *
    * 校验请求客户端是否属于指定活动传输，
    * 终止服务端对应传输状态，并通知仍在线的另一端。
    *
    * @param clientId 发起终止请求的客户端 ID。
    * @param body FileAbortRequest 消息体。
    */
    void handleFileAbortRequest(quint32 clientId,const QByteArray&body);

    /**
    * @brief 向指定客户端发送文件传输终止通知。
    *
    * @param clientId 接收通知的客户端 ID。
    * @param transferId 文件传输唯一标识。
    * @param reason 文件传输终止原因。
    * @return 数据成功提交到目标 Socket 写缓冲时返回 true，否则返回 false。
    */
    bool sendFileAbort(quint32 clientId,
                       const QString&transferId,Protocol::FileAbortReason reason);

    /**
    * @brief 因服务端检测到异常而终止指定活动文件传输。
    *
    * 删除服务端的活动传输状态，向传输两端发送 FileAbort，
    * 并清理该传输可能遗留的服务端背压状态。
    *
    * @param transferId 要终止的文件传输唯一标识。
    * @param reason 文件传输终止原因。
    */
    void abortTransferFromServer(const QString&transferId,Protocol::FileAbortReason reason);

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

    static constexpr int TRANSFER_WATCH_INTERVAL_MS=5000;

    static constexpr qint64 TRANSFER_STALL_TIMEOUT_MS=60000;

    struct ClientSession{
        QTcpSocket *socket{nullptr};
        QByteArray receiveBuffer;

        bool readPaused{false};
        quint32 blockedReceiverId{0};
    };

    enum class TransferPhase {
        Transferring,
        AwaitingReceiverComplete
    };

    struct ActiveTransfer{
        quint32 receiverId{0};
        quint32 senderId{0};
        qint64 fileSize{0};
        qint64 nextOffset{0};
        TransferPhase phase{TransferPhase::Transferring};
        qint64 lastActivityMs{0};
    };

    QTcpServer *m_server;
    QTimer *m_transferWatchTimer;
    QElapsedTimer m_transferClock;
    QHash<quint32,ClientSession>m_sessions;
    quint32 m_nextClientId{1};

    QHash<QString,ActiveTransfer>m_activeTransfers;
};

#endif // TCPSERVER_H
