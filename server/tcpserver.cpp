#include "tcpserver.h"

#include "packetparser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include"textmessagecodec.h"
#include"packetcodec.h"

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
    const auto sockets=m_sessions.keys();
    for(QTcpSocket*socket:sockets){
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

        const quint32 clientId=m_nextClientId++;
        ClientSession session;
        session.clientId=clientId;
        m_sessions.insert(socket,session);
        connect(socket,&QTcpSocket::readyRead,this,[socket,this](){
            handleClientReadyRead(socket);
        });

        emit clientCountChanged(m_sessions.size());
        emit logMessage(QString("Client %1 connected:%2:%3")
                            .arg(clientId)
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
        connect(socket,&QTcpSocket::disconnected,this,[this,socket,clientId](){
            m_sessions.remove(socket);
            emit clientCountChanged(m_sessions.size());
            emit logMessage(QString("Client %1 disconnected").arg(clientId));
            socket->deleteLater();
        });
    }
}

void TcpServer::handleClientReadyRead(QTcpSocket *socket)
{
    auto it=m_sessions.find(socket);
    if(it==m_sessions.end()){
        emit logMessage("Received data from unknown client");
        return;
    }
    ClientSession &session=it.value();
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
            emit logMessage(QString("Client%1 sent an unexpected message type").arg(session.clientId));
            receiveBuffer.clear();
            socket->disconnectFromHost();
            break;
        }
        QString content;
        if(!Protocol::TextMessageCodec::decodeSendRequest(packet.body,content)){
            emit logMessage(QString("Client%1 sent an invalid text request").arg(session.clientId));
            continue;
        }

        Protocol::Packet outgoingPacket;
        const quint32 senderId=session.clientId;
        emit logMessage(QString("Client %1: %2").arg(senderId).arg(content));
        outgoingPacket.type=Protocol::MessageType::TextMessage;
        outgoingPacket.body=Protocol::TextMessageCodec::encodeTextMessage(senderId,content);
        QByteArray data=Protocol::PacketCodec::encode(outgoingPacket);

        const auto sockets=m_sessions.keys();
        for(QTcpSocket*client:sockets){
            if(client==socket){
                continue;
            }
            if(client->state()==QAbstractSocket::ConnectedState){
                client->write(data);
            }
        }
    }
}
