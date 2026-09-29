#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include<QFileDialog>
#include <QFileInfo>
#include"connectionmanager.h"
#include"filetransfermanager.h"

MainWindow::MainWindow(const QString &databasePath,QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    ,m_connectionManager(new ConnectionManager(this))
    ,m_fileTransferManager(new FileTransferManager(m_connectionManager,this))
    ,m_databaseManager(databasePath)
{
    ui->setupUi(this);
    handleConnectionStateChanged(
        ConnectionManager::ConnectionState::Disconnected);

    QString databaseError;
    if(!m_databaseManager.initialize(databaseError)){
        ui->statusbar->showMessage(
            QStringLiteral("Database initialization failed:%1").arg(databaseError));
    }
    else{
        QList<MessageRecord>messages;
        if(!m_databaseManager.loadMessages(messages,databaseError)){
            ui->statusbar->showMessage(
                QStringLiteral("Failed to load message history:%1").arg(databaseError));
        }else{
            for(const MessageRecord &record:messages){
                if(record.direction==MessageDirection::Sent){
                    ui->messageDisplay->appendPlainText(
                        QStringLiteral("Me:%1").arg(record.content));
                }else{
                    ui->messageDisplay->appendPlainText(
                        QStringLiteral("Received:%1").arg(record.content));
                }
            }
        }
    }

    connect(m_connectionManager,&ConnectionManager::disconnected,this,&MainWindow::handleDisconnected);
    connect(m_connectionManager,&ConnectionManager::connectionError,this,&MainWindow::handleConnectionError);
    connect(m_connectionManager,
            &ConnectionManager::operationError,
            this,
            [this](const QString &message)
            {
                ui->statusbar->showMessage(message);
            });
    connect(m_connectionManager,&ConnectionManager::textMessageReceived,this,&MainWindow::handleTextMessageReceived);
    connect(m_connectionManager,
            &ConnectionManager::onlineClientListReceived,this,
            &MainWindow::handleOnlineClientListReceived);
    connect(ui->receiverComboBox,&QComboBox::currentIndexChanged,this,
            [this](int index){
                ui->sendBtn->setEnabled(index>=0);
                ui->sendFileBtn->setEnabled(index>=0&&!m_selectedFilePath.isEmpty());
    });

    connect(m_fileTransferManager,&FileTransferManager::sendProgressChanged,
            this,[this](qint64 sentBytes,qint64 totalBytes){
        int progress=100;
        if(totalBytes>0){
            progress=static_cast<double>(
                static_cast<double>(sentBytes)/static_cast<double>(totalBytes)*100.0);
        }
        ui->fileProgressBar->setValue(progress);
    });

    connect(m_fileTransferManager,&FileTransferManager::fileReceiveCompleted,
            this,[this](const QString&filePath){
        statusBar()->showMessage(QString("文件接收完成：%1").arg(filePath),5000);
    });

    connect(m_connectionManager,&ConnectionManager::connectionStateChanged,
            this,&MainWindow::handleConnectionStateChanged);

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
        ui->statusbar->showMessage("Host cannot be empty");
        return;
    }
    m_connectionManager->connectToServer(host,port);
}


void MainWindow::on_disconnectBtn_clicked()
{
    m_connectionManager->disconnectFromServer();
}

void MainWindow::handleDisconnected()
{
    ui->receiverComboBox->clear();
    ui->receiverComboBox->setDisabled(true);
}

void MainWindow::handleConnectionError(const QString &message)
{
    ui->statusbar->showMessage(QString("Connection error:%1").arg(message));
}


void MainWindow::on_sendBtn_clicked()
{
    QString text=ui->messageLineEdit->text();
    if(text.trimmed().isEmpty()){
        return;
    }
    const QVariant receiverData=ui->receiverComboBox->currentData();
    if(!receiverData.isValid()){
        ui->statusbar->showMessage(
            QStringLiteral("Please select an online client."));
        return;
    }
    const quint32 receiverId=receiverData.toUInt();
    if(m_connectionManager->sendTextMessage(receiverId,text)){
        ui->messageDisplay->appendPlainText(QString("Me: %1").arg(text));
        ui->messageLineEdit->clear();
        QString databaseError;
        if(!m_databaseManager.saveMessage(MessageDirection::Sent,text,databaseError)){
            ui->statusbar->showMessage(
                QStringLiteral("Failed to save message:%1").arg(databaseError));
        }
    }
}

void MainWindow::handleConnectionStateChanged(ConnectionManager::ConnectionState state)
{
    switch(state){

    case ConnectionManager::ConnectionState::Disconnected:
        ui->connectBtn->setEnabled(true);
        ui->disconnectBtn->setEnabled(false);
        ui->sendBtn->setEnabled(false);
        ui->sendFileBtn->setEnabled(false);
        ui->receiverComboBox->setEnabled(false);

        statusBar()->showMessage("Disconnected");
        break;

    case ConnectionManager::ConnectionState::Connecting:
        ui->connectBtn->setEnabled(false);
        ui->disconnectBtn->setEnabled(true);
        ui->sendBtn->setEnabled(false);
        ui->sendFileBtn->setEnabled(false);
        ui->receiverComboBox->setEnabled(false);

        statusBar()->showMessage("Connecting...");
        break;

    case ConnectionManager::ConnectionState::Connected:
        ui->connectBtn->setEnabled(false);
        ui->disconnectBtn->setEnabled(true);
        ui->receiverComboBox->setEnabled(true);
        ui->sendBtn->setEnabled(
            ui->receiverComboBox->currentData().isValid());
        ui->sendFileBtn->setEnabled(
            ui->receiverComboBox->currentData().isValid()
            &&!m_selectedFilePath.isEmpty());

        statusBar()->showMessage("Connected");
        break;

    case ConnectionManager::ConnectionState::Reconnecting:
        ui->connectBtn->setEnabled(false);
        ui->disconnectBtn->setEnabled(true);
        ui->sendBtn->setEnabled(false);
        ui->sendFileBtn->setEnabled(false);
        ui->receiverComboBox->setEnabled(false);

        statusBar()->showMessage("Reconnecting...");
        break;
    }
}

void MainWindow::handleTextMessageReceived(const quint32 senderId,const QString &text)
{
    ui->messageDisplay->appendPlainText(
        QString("Client%1: %2").arg(senderId).arg(text));
    QString databaseError;
    if(!m_databaseManager.saveMessage(MessageDirection::Received,text,databaseError)){
        ui->statusbar->showMessage(
             QStringLiteral("Failed to save message:%1").arg(databaseError));
    }
}

void MainWindow::handleOnlineClientListReceived(quint32 selfId, const QList<quint32> &clientIds)
{
    const QVariant previousReceiverData=ui->receiverComboBox->currentData();
    ui->receiverComboBox->clear();
    for(quint32 clientId:clientIds){
        if(clientId==selfId){
            continue;
        }
        ui->receiverComboBox->addItem(
            QStringLiteral("Client %1").arg(clientId),
            QVariant::fromValue(clientId));
    }
    int receiverIndex=-1;
    if(previousReceiverData.isValid()){
        receiverIndex=ui->receiverComboBox->findData(previousReceiverData);
    }
    ui->receiverComboBox->setCurrentIndex(receiverIndex);
    ui->sendBtn->setEnabled(receiverIndex>=0);
}


void MainWindow::on_selectFileBtn_clicked()
{
    QString filePath=QFileDialog::getOpenFileName(this,"选择文件");
    if(filePath.isEmpty()){
        return;
    }
    m_selectedFilePath=filePath;
    ui->filePathLineEdit->setText(filePath);

    ui->sendFileBtn->setEnabled(ui->receiverComboBox->currentData().isValid());
    ui->fileProgressBar->setValue(0);
}


void MainWindow::on_sendFileBtn_clicked()
{
    if(m_selectedFilePath.isEmpty()){
        ui->statusbar->showMessage("请先选择要发送的文件");
        return;
    }

    QFileInfo fileInfo(m_selectedFilePath);
    if(!fileInfo.exists()||!fileInfo.isFile()||!fileInfo.isReadable()){
        ui->statusbar->showMessage("当前文件不存在或无法读取，请重新选择");
        return;
    }

    const QVariant receiverData=ui->receiverComboBox->currentData();
    if(!receiverData.isValid()){
        ui->statusbar->showMessage("请选择接受客户端");
        return;
    }

    const quint32 receiverId=receiverData.toUInt();

    if(!m_fileTransferManager->sendFile(m_selectedFilePath,receiverId)){
        ui->statusbar->showMessage("文件发送启动失败");
        return;
    }

    ui->statusbar->showMessage("文件传输开始");
}

