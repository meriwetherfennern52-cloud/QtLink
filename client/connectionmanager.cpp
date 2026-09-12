#include "connectionmanager.h"

#include <QTcpSocket>
#include "packetparser.h"
#include<QDataStream>
#include"packetcodec.h"
#include"textmessagecodec.h"

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject{parent},m_socket(new QTcpSocket(this))
{
    connect(m_socket,&QTcpSocket::connected,this,&ConnectionManager::connected);
    connect(m_socket,&QTcpSocket::disconnected,this,&ConnectionManager::handleSocketDisconnected);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error){
        emit connectionError(m_socket->errorString());
    });
    connect(m_socket,&QTcpSocket::readyRead,this,&ConnectionManager::handleReadyRead);
}

void ConnectionManager::connectToServer(const QString &host, quint16 port)
{
    m_receiveBuffer.clear();
    m_socket->connectToHost(QHostAddress(host),port);
}

void ConnectionManager::disconnectFromServer()
{
    m_socket->disconnectFromHost();
}

bool ConnectionManager::sendTextMessage(const QString &text)
{
    if(m_socket->state()!=QAbstractSocket::ConnectedState){
        emit connectionError("Cannot send message:not connected to server.");
        return false;
    }
    if(text.trimmed().isEmpty()){
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::TextMessage;
    packet.body=Protocol::textmessagecodec::encode(text);

    if(packet.body.size()>static_cast<qsizetype>(Protocol::MAX_BODY_SIZE)){
        emit connectionError("Message is too large");
        return false;
    }
    QByteArray data=Protocol::PacketCodec::encode(packet);
    if(m_socket->write(data)<0){
        emit connectionError(m_socket->errorString());
        return false;
    }

    return true;
}

void ConnectionManager::handleReadyRead()
{
    QByteArray newData=m_socket->readAll();
    m_receiveBuffer.append(newData);
    // qDebug() << "new data size:" << newData.size()
    //          << "receive buffer size:" << m_receiveBuffer.size();
    while(true){
        Protocol::Packet packet;
        Protocol::ParseResult result=Protocol::PacketParser::tryParse(
            m_receiveBuffer,
            packet
            );
        if(result==Protocol::ParseResult::NeedMoreData){
            break;
        }
        if(result==Protocol::ParseResult::InvalidData){
            emit connectionError("Invalid QtLink packet");
            m_receiveBuffer.clear();
            disconnectFromServer();
            break;
        }
        if(packet.type==Protocol::MessageType::TextMessage){

            QString content;
            if(!Protocol::textmessagecodec::decode(packet.body,content)){
                emit connectionError("Invalid text message");
                continue;
            }
            emit textMessageReceived(content);
        }
    }
}

void ConnectionManager::handleSocketDisconnected()
{
    m_receiveBuffer.clear();
    emit disconnected();
}
