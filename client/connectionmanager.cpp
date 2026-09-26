#include "connectionmanager.h"

#include <QTcpSocket>
#include "packetparser.h"
#include<QDataStream>
#include"packetcodec.h"
#include"textmessagecodec.h"
#include"onlineclientlistcodec.h"
#include"filetransfercontrolcodec.h"
#include"filechunkcodec.h"

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject{parent},m_socket(new QTcpSocket(this))
{
    connect(m_socket,&QTcpSocket::connected,this,&ConnectionManager::connected);
    connect(m_socket,&QTcpSocket::disconnected,this,&ConnectionManager::handleSocketDisconnected);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error){
        emit connectionError(m_socket->errorString());
    });
    connect(m_socket,&QTcpSocket::readyRead,this,&ConnectionManager::handleReadyRead);
    connect(m_socket,&QTcpSocket::bytesWritten,this,[this](qint64 bytes){
        emit socketBytesWritten(bytes);
    });
}

void ConnectionManager::connectToServer(const QString &host, quint16 port)
{
    if(m_socket->state()!=QAbstractSocket::UnconnectedState){
        emit connectionError("Connection is already active");
        return;
    }
    m_receiveBuffer.clear();
    m_socket->connectToHost(host,port);
}

void ConnectionManager::disconnectFromServer()
{
    m_socket->disconnectFromHost();
}

bool ConnectionManager::sendTextMessage(quint32 receiverId,const QString &text)
{
    if(text.trimmed().isEmpty()){
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::TextSendRequest;
    packet.body=Protocol::TextMessageCodec::encodeSendRequest(receiverId,text);

    return writePacket(packet);
}

bool ConnectionManager::sendFileStartRequest(
    quint32 receiverId,const QString &transferId,
    const QString &fileName,qint64 fileSize)
{
    QByteArray body=Protocol::FileTransferControlCodec::encodeStartRequest(
        receiverId,transferId,fileName,fileSize);
    if(body.isEmpty()){
        emit operationError("Failed to encode file start request");
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileStartRequest;
    packet.body=body;

    return writePacket(packet);
}

bool ConnectionManager::sendFileChunkRequest(const QString &transferId, qint64 offset,
                                             const QByteArray &chunkData)
{
    if(chunkData.isEmpty()){
        return false;
    }

    QByteArray body=Protocol::FileChunkCodec::encode(transferId,offset,chunkData);
    if(body.isEmpty()){
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileChunkRequest;
    packet.body=body;

    return writePacket(packet);
}

qint64 ConnectionManager::pendingBytesToWrite() const
{
    return m_socket->bytesToWrite();
}

bool ConnectionManager::sendFileEndRequest(const QString &transferId)
{
    QByteArray body=Protocol::FileTransferControlCodec::encodeEnd(transferId);
    if(body.isEmpty()){
        emit operationError(
            "Failed to encode file end request.");
        return false;
    }

    Protocol::Packet packet;
    packet.body=body;
    packet.type=Protocol::MessageType::FileEndRequest;

    return writePacket(packet);
}

bool ConnectionManager::sendFileAbortRequest(const QString &transferId, Protocol::FileAbortReason reason)
{
    const QByteArray body=Protocol::FileTransferControlCodec::encodeAbort(transferId,reason);

    if(body.isEmpty()){
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileAbortRequest;
    packet.body=body;

    return writePacket(packet);
}

bool ConnectionManager::sendFileCompleteRequest(const QString &transferId)
{
    const QByteArray body=Protocol::FileTransferControlCodec::encodeComplete(transferId);
    if(body.isEmpty()){
        return false;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileCompleteRequest;
    packet.body=body;

    return writePacket(packet);
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
            emit operationError("Invalid QtLink packet");
            m_receiveBuffer.clear();
            disconnectFromServer();
            break;
        }

        switch(packet.type){
        case Protocol::MessageType::TextMessage:{
            quint32 senderId=0;
            QString content;
            if(!Protocol::TextMessageCodec::decodeTextMessage(
                    packet.body,senderId,content)){
                emit operationError("Invalid text message");
                break;
            }
            emit textMessageReceived(senderId,content);
            break;
        }
        case Protocol::MessageType::OnlineClientList:{
            quint32 selfId=0;
            QList<quint32>clientIds;
            if(!Protocol::OnlineClientListCodec::decode(
                    packet.body,selfId,clientIds)){
                emit operationError(QStringLiteral(
                        "Failed to decode online client list."));
                break;
            }
            emit onlineClientListReceived(selfId,clientIds);
            break;
        }
        case Protocol::MessageType::FileStart:{
            quint32 senderId;
            QString transferId;
            QString fileName;
            qint64 fileSize;
            if(!Protocol::FileTransferControlCodec::decodeStart(
                    packet.body,senderId,transferId,fileName,fileSize)){
                emit operationError("Invalid file start message from server.");
                break;
            }
            emit fileStartReceived(senderId,transferId,fileName,fileSize);
            break;
        }
        case Protocol::MessageType::FileChunk:{
            QString transferId;
            qint64 offset=0;
            QByteArray chunkData;

            if(!Protocol::FileChunkCodec::decode(packet.body,transferId,offset,chunkData)){
                emit operationError("Invalid file chunk from server.");
                break;
            }

            emit fileChunkReceived(transferId,offset,chunkData);

            break;
        }
        case Protocol::MessageType::FileEnd:{
            QString transferId;
            if(!Protocol::FileTransferControlCodec::decodeEnd(packet.body,transferId)){
                emit operationError("Invalid file end message from server.");
                break;
            }
            emit fileEndReceived(transferId);
            break;
        }
        case Protocol::MessageType::FileAbort:{
            QString transferId;

            Protocol::FileAbortReason reason=Protocol::FileAbortReason::ProtocolError;

            if(!Protocol::FileTransferControlCodec::decodeAbort(packet.body,transferId,reason)){
                emit operationError("Invalid file abort message from server.");
                disconnectFromServer();
                return;
            }

            emit fileAbortReceived(transferId,reason);
            break;
        }
        case Protocol::MessageType::FileComplete:{
            QString transferId;
            if(!Protocol::FileTransferControlCodec::decodeComplete(packet.body,transferId)){
                emit operationError("Invalid file complete message from server.");
                disconnectFromServer();
                return;
            }

            emit fileCompleteReceived(transferId);
            break;
        }
        default:
            emit operationError(QStringLiteral("Unexpected message type from server."));
            break;
        }


    }
}

void ConnectionManager::handleSocketDisconnected()
{
    m_receiveBuffer.clear();
    emit disconnected();
}

bool ConnectionManager::writePacket(const Protocol::Packet &packet)
{
    if(m_socket->state()!=QAbstractSocket::ConnectedState){
        emit operationError("Not connected to server.");
        return false;
    }

    if(packet.body.size()>static_cast<qsizetype>(Protocol::MAX_BODY_SIZE)){
        emit operationError("Message id too large");
        return false;
    }

    QByteArray data=Protocol::PacketCodec::encode(packet);
    if(m_socket->write(data)<0){
        emit operationError(m_socket->errorString());
        return false;
    }

    return true;
}
