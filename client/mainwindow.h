#ifndef MAINWINDOW_H
#define MAINWINDOW_H

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
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_connectBtn_clicked();

    void on_disconnectBtn_clicked();

    void handleConnected();

    void handleDisconnected();

    void handleConnectionError(const QString &message);

private:
    Ui::MainWindow *ui;
    ConnectionManager*m_connectionManager;
};
#endif // MAINWINDOW_H
