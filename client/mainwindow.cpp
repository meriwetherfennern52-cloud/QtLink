#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include"connectionmanager.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    ,m_connectionManager(new ConnectionManager(this))
{
    ui->setupUi(this);
    connect(m_connectionManager,&ConnectionManager::connected,this,&MainWindow::handleConnected);
    connect(m_connectionManager,&ConnectionManager::disconnected,this,&MainWindow::handleDisconnected);
    connect(m_connectionManager,&ConnectionManager::connectionError,this,&MainWindow::handleConnectionError);
    ui->statusbar->showMessage("Disconnected");
    connect(m_connectionManager,&ConnectionManager::textMessageReceived,this,&MainWindow::handleTextMessageReceived);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_connectBtn_clicked()
{
    const QString host=ui->ipLineEdit->text().trimmed();
    const quint16 port=static_cast<quint16>(ui->portSpinBox->value());
    if(host.isEmpty()){
        ui->statusbar->showMessage("Host connot be empty");
    }
    ui->statusbar->showMessage("Connecting...");
    ui->connectBtn->setDisabled(true);
    ui->disconnectBtn->setDisabled(true);
    m_connectionManager->connectToServer(host,port);
}


void MainWindow::on_disconnectBtn_clicked()
{
    ui->statusbar->showMessage("Disconnecting...");
    ui->disconnectBtn->setDisabled(true);
    m_connectionManager->disconnectFromServer();
}

void MainWindow::handleConnected()
{
    ui->statusbar->showMessage("Connected");
    ui->disconnectBtn->setEnabled(true);
    ui->connectBtn->setDisabled(true);
    ui->sendBtn->setEnabled(true);
}

void MainWindow::handleDisconnected()
{
    ui->statusbar->showMessage("Disconnected");
    ui->connectBtn->setEnabled(true);
    ui->disconnectBtn->setDisabled(true);
    ui->sendBtn->setDisabled(true);
}

void MainWindow::handleConnectionError(const QString &message)
{
    ui->statusbar->showMessage(QString("Connection error:%1").arg(message));
    ui->connectBtn->setEnabled(true);
    ui->disconnectBtn->setDisabled(true);
    ui->sendBtn->setDisabled(true);
}


void MainWindow::on_sendBtn_clicked()
{
    QString text=ui->messageLineEdit->text();
    if(text.trimmed().isEmpty()){
        return;
    }
    if(m_connectionManager->sendTextMessage(text)){
        ui->messageDisplay->appendPlainText(QString("Me: %1").arg(text));
        ui->messageLineEdit->clear();
    }
}

void MainWindow::handleTextMessageReceived(const QString &text)
{
    ui->messageDisplay->appendPlainText(QString("Server: %1").arg(text));
}

