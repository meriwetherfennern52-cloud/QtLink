#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "databasemanager.h"

#include <QMainWindow>


QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ConnectionManager;

/**
 * @brief QtLink 客户端主窗口。
 *
 * 负责用户交互和界面状态展示，
 * 不直接处理底层 TCP 网络通信。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
    * @brief 创建客户端主窗口。
    *
    * @param databasePath SQLite 数据库文件路径。
    * @param parent 父对象。
    */
    explicit MainWindow(const QString &databasePath,QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_connectBtn_clicked();

    void on_disconnectBtn_clicked();

    void handleConnected();

    void handleDisconnected();

    void handleConnectionError(const QString &message);

    void on_sendBtn_clicked();

    /**
    * @brief 处理服务器下发的文本消息。
    *
    * @param senderId 消息发送者的连接 ID。
    * @param text 接收到的文本内容。
    */
    void handleTextMessageReceived(const quint32 senderId,const QString &text);

    /**
    * @brief 更新服务器下发的在线客户端列表。
    *
    * @param selfId 当前客户端自身的连接 ID。
    * @param clientIds 当前所有在线客户端的连接 ID。
    */
    void handleOnlineClientListReceived(quint32 selfId,const QList<quint32>&clientIds);


private:
    Ui::MainWindow *ui;
    ConnectionManager*m_connectionManager;
    DatabaseManager m_databaseManager;
};
#endif // MAINWINDOW_H
