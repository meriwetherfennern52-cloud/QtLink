#include "tcpserver.h"

#include "packetparser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include<QTimer>
#include"packetcodec.h"
#include"textmessagecodec.h"

TcpServer::TcpServer(QObject *parent)
    : QObject{parent},m_server(new QTcpServer(this))
{
    connect(m_server,&QTcpServer::newConnection,this,&TcpServer::handleNewConnection);
}

bool TcpServer::startServer(quint16 port)
{
    if(m_server->isListening()){
        return true;
    }
    if(!m_server->listen(QHostAddress::Any,port)){
        emit serverError(m_server->errorString());
        return false;
    }
    emit logMessage(QString("Server started on port %1").arg(port));
    return true;
}

void TcpServer::stopServer()
{
    if(!m_server->isListening()){
        return;
    }
    m_server->close();
    for(QTcpSocket*socket:m_clients){
        socket->disconnectFromHost();
    }
    emit logMessage("Server stopped");
}

void TcpServer::handleNewConnection()
{
    while(m_server->hasPendingConnections()){
        QTcpSocket*socket=m_server->nextPendingConnection();
        if(!socket){
            continue;
        }
        connect(socket,&QTcpSocket::readyRead,this,[socket,this](){
            handleClientReadyRead(socket);
        });
        m_clients.append(socket);
        m_receiveBuffers.insert(socket, QByteArray());
        emit clientCountChanged(m_clients.size());
        emit logMessage(QString("Client connected:%1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
        connect(socket,&QTcpSocket::disconnected,this,[this,socket](){
            m_clients.removeOne(socket);
            m_receiveBuffers.remove(socket);
            emit clientCountChanged(m_clients.size());
            emit logMessage("Client disconnected");
            socket->deleteLater();
        });
    }
}

void TcpServer::handleClientReadyRead(QTcpSocket *socket)
{
    QByteArray &receiveBuffer=m_receiveBuffers[socket];
    QByteArray newData=socket->readAll();
    receiveBuffer.append(newData);

    while(true){
        Protocol::Packet packet;
        Protocol::ParseResult result=Protocol::PacketParser::tryParse(receiveBuffer,packet);
        if(result==Protocol::ParseResult::NeedMoreData){
            break;
        }
        if(result==Protocol::ParseResult::InvalidData){
            emit logMessage("Received invalid QtLink packet");
            receiveBuffer.clear();
            socket->disconnectFromHost();
            break;
        }

        QString content;
        if(!Protocol::textmessagecodec::decode(packet.body,content)){
            emit logMessage("Invalid text message");
            continue;
        }
        emit logMessage(QString("Received message: %1").arg(content));

        QByteArray data=Protocol::PacketCodec::encode(packet);
        for(QTcpSocket*client:m_clients){
            if(client==socket){
                continue;
            }
            if(client->state()==QAbstractSocket::ConnectedState){
                client->write(data);
            }
        }
    }
}
