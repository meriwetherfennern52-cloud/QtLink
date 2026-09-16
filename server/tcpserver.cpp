#include "tcpserver.h"

#include "packetparser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include"textmessagecodec.h"
#include"packetcodec.h"
#include"onlineclientlistcodec.h"
#include <algorithm>

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
    if(m_server->isListening()){
        m_server->close();
    }

    QList<QTcpSocket*>sockets;
    for(auto it=m_sessions.cbegin();it!=m_sessions.cend();it++){
        sockets.append(it.value().socket);
    }
    for(QTcpSocket*socket:sockets){
        if(socket){
            socket->disconnectFromHost();
        }
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

        const quint32 clientId=m_nextClientId++;
        ClientSession session;
        session.socket=socket;
        m_sessions.insert(clientId,session);

        connect(socket,&QTcpSocket::readyRead,this,[clientId,this](){
            handleClientReadyRead(clientId);
        });

        emit clientCountChanged(m_sessions.size());
        emit logMessage(QString("Client %1 connected:%2:%3")
                            .arg(clientId)
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
        connect(socket,&QTcpSocket::disconnected,this,[this,socket,clientId](){
            m_sessions.remove(clientId);
            emit clientCountChanged(m_sessions.size());
            emit logMessage(QString("Client %1 disconnected").arg(clientId));
            broadcastOnlineClientList();
            socket->deleteLater();
        });
        connect(socket,
                &QTcpSocket::errorOccurred,
                this,
                [this,socket,clientId](
                    QAbstractSocket::SocketError){

                    emit logMessage(
                        QString("Client %1 socket error: %2")
                            .arg(clientId)
                            .arg(socket->errorString()));
                });
        broadcastOnlineClientList();
    }
}

void TcpServer::handleClientReadyRead(quint32 clientId)
{
    auto it=m_sessions.find(clientId);
    if(it==m_sessions.end()){
        emit logMessage("Received data from unknown client");
        return;
    }
    ClientSession &session=it.value();
    QTcpSocket *socket=session.socket;
    if(!socket){
        emit logMessage("Client session has no socket");
        return;
    }
    QByteArray &receiveBuffer=session.receiveBuffer;
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

        if(packet.type!=Protocol::MessageType::TextSendRequest){
            emit logMessage(QString("Client%1 sent an unexpected message type").arg(clientId));
            receiveBuffer.clear();
            socket->disconnectFromHost();
            break;
        }
        quint32 receiverId;
        QString content;
        if(!Protocol::TextMessageCodec::decodeSendRequest(packet.body,receiverId,content)){
            emit logMessage(QString("Client%1 sent an invalid text request").arg(clientId));
            continue;
        }

        if(receiverId==clientId){
            emit logMessage(
                QString("Client %1 attempted to send a message to itself")
                    .arg(clientId));
            continue;
        }

        auto receiverIt=m_sessions.find(receiverId);

        if(receiverIt==m_sessions.end()){
            emit logMessage(
                QString("Target client %1 is not connected")
                    .arg(receiverId));
            continue;
        }

        QTcpSocket*receiverSocket=receiverIt.value().socket;

        if(!receiverSocket
            || receiverSocket->state()!=QAbstractSocket::ConnectedState){

            emit logMessage(
                QString("Target client %1 is not connected")
                    .arg(receiverId));
            continue;
        }

        const quint32 senderId=clientId;

        Protocol::Packet outgoingPacket;
        outgoingPacket.type=Protocol::MessageType::TextMessage;
        outgoingPacket.body=
            Protocol::TextMessageCodec::encodeTextMessage(
                senderId,
                content);

        if(outgoingPacket.body.size()>
            static_cast<qsizetype>(Protocol::MAX_BODY_SIZE)){

            emit logMessage(
                QString("Message from Client %1 exceeds maximum body size after encoding")
                    .arg(senderId));

            continue;
        }
        QByteArray data=
            Protocol::PacketCodec::encode(outgoingPacket);

        const qint64 writtenBytes=
            receiverSocket->write(data);

        if(writtenBytes<0){
            emit logMessage(
                QString("Failed to send message to Client %1: %2")
                    .arg(receiverId)
                    .arg(receiverSocket->errorString()));
            continue;
        }

        emit logMessage(
            QString("Client %1 -> Client %2: %3")
                .arg(senderId)
                .arg(receiverId)
                .arg(content));
    }
}

void TcpServer::broadcastOnlineClientList()
{
    QList<quint32>clientIds;

    for(auto it=m_sessions.cbegin();
         it!=m_sessions.cend();
         it++){

        QTcpSocket*socket=it.value().socket;

        if(socket
            && socket->state()==
                   QAbstractSocket::ConnectedState){

            clientIds.append(it.key());
        }
    }
    std::sort(clientIds.begin(),clientIds.end());

    for(auto it=m_sessions.cbegin();it!=m_sessions.cend();it++){
        const quint32 selfId=it.key();
        QTcpSocket*socket=it.value().socket;
        if(!socket||socket->state()!=QAbstractSocket::ConnectedState){
            continue;
        }

        Protocol::Packet packet;
        packet.type=Protocol::MessageType::OnlineClientList;
        packet.body=Protocol::OnlineClientListCodec::encode(selfId,clientIds);
        const QByteArray data=Protocol::PacketCodec::encode(packet);
        const qint64 writtenBytes=
            socket->write(data);

        if(writtenBytes<0){
            emit logMessage(
                QString("Failed to send online client list to Client %1: %2")
                    .arg(selfId)
                    .arg(socket->errorString()));
        }
    }
}
