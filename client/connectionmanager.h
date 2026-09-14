#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QObject>
// #include <QTcpSocket>
#include <QByteArray>
class QTcpSocket; //前置声明，.cpp里再写↑，减少头文件之间的耦合和不必要的编译依赖

/**
 * @brief 管理客户端与服务器之间的 TCP 连接。
 *
 * ConnectionManager
 * 负责 QTcpSocket 的生命周期、服务器连接与断开，
 * 并通过信号向上层通知连接状态和网络错误。
 *
 * 当前类不负责：
 * - 应用层协议解析
 * - 消息业务
 * - 心跳检测
 * - 自动重连
 * - 文件传输
 *
 * 当前阶段对象运行在 GUI 主线程中，依赖 Qt 事件循环进行异步网络通信。
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

    /**
    * @brief 向当前已连接的服务器发送文本消息。
    *
    * 将文本转换为 UTF-8 字节，封装为 QtLink TextMessage 数据包后发送。
    *
    * @param text 要发送的文本内容。
    * @return 成功提交给套接字发送时返回 true，否则返回 false。
    */
    bool sendTextMessage(const QString &text);

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

    /**
    * @brief 通知上层收到一条服务器下发的文本消息。
    *
    * @param senderId 消息发送者的连接 ID。
    * @param text 文本消息内容。
    */
    void textMessageReceived(const quint32 senderId,const QString &text);

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

private:
    QTcpSocket*m_socket;
    QByteArray m_receiveBuffer;
};

#endif // CONNECTIONMANAGER_H
