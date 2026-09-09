#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class TcpServer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_startServerBtn_clicked();

    void on_stopServerBtn_clicked();

private:
    Ui::MainWindow *ui;
    TcpServer*m_tcpServer;
};
#endif // MAINWINDOW_H
