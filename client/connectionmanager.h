#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <QObject>
// #include <QTcpSocket>
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
     * @brief 网络连接发生错误。
     * @param message 可供上层显示的错误信息。
     */
    void connectionError(const QString &message);

private:
    QTcpSocket*m_socket;
};

#endif // CONNECTIONMANAGER_H
