#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include"connectionmanager.h"

MainWindow::MainWindow(const QString &databasePath,QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    ,m_connectionManager(new ConnectionManager(this))
    ,m_databaseManager(databasePath)
{
    ui->setupUi(this);
    ui->statusbar->showMessage("Disconnected");

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
            qDebug() << "Loaded messages:"
                     << messages.size();
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

    connect(m_connectionManager,&ConnectionManager::connected,this,&MainWindow::handleConnected);
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
                ui->sendBtn->setEnabled(true);
    });
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
    ui->sendBtn->setEnabled(false);
    ui->receiverComboBox->setEnabled(true);
}

void MainWindow::handleDisconnected()
{
    ui->statusbar->showMessage("Disconnected");
    ui->connectBtn->setEnabled(true);
    ui->disconnectBtn->setDisabled(true);
    ui->sendBtn->setDisabled(true);
    ui->receiverComboBox->clear();
    ui->receiverComboBox->setDisabled(true);
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

