#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
class QTcpServer;
class QTcpSocket;
/**
 * @brief QtLink 服务端 TCP 连接管理类。
 *
 * 负责监听 TCP 端口、接受客户端连接、维护当前客户端连接，
 * 并通过信号向 UI 层通知服务器状态、在线人数和运行日志。
 *
 * 当前不负责应用层协议解析和消息转发。
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

signals:
    void clientCountChanged(int count);

    void logMessage(const QString &message);

    void serverError(const QString &message);

private slots:
    void handleNewConnection();

private:
    QTcpServer *m_server;
    QList<QTcpSocket*>m_clients;
};

#endif // TCPSERVER_H
