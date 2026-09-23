#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include"tcpserver.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow),m_tcpServer(new TcpServer(this))
{
    ui->setupUi(this);

    ui->statusbar->showMessage("Stopped");
    connect(m_tcpServer,&TcpServer::clientCountChanged,this,[this](int count){
        ui->onlineCountLabel->setText(QString::number(count));
    });
    connect(m_tcpServer,&TcpServer::logMessage,this,[this](const QString &message){
        ui->serverLogTextEdit->appendPlainText(message);
    });
    connect(m_tcpServer,&TcpServer::serverError,this,[this](const QString &message){
        ui->statusbar->showMessage(QString("Server error: %1").arg(message));
    });

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_startServerBtn_clicked()
{
    const quint16 port=static_cast<quint16>(ui->portSpinBox->value());
    if(!m_tcpServer->startServer(port)){
        return;
    }
    ui->statusbar->showMessage(QString("Listening on port %1").arg(port));
    ui->startServerBtn->setDisabled(true);
    ui->stopServerBtn->setEnabled(true);
    ui->portSpinBox->setDisabled(true);
}


void MainWindow::on_stopServerBtn_clicked()
{
    m_tcpServer->stopServer();
    ui->statusbar->showMessage("Stopped");
    ui->startServerBtn->setEnabled(true);
    ui->stopServerBtn->setDisabled(true);
    ui->portSpinBox->setEnabled(true);
}

