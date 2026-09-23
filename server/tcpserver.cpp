#include "tcpserver.h"

#include "packetparser.h"
#include <QTcpServer>
#include <QTcpSocket>
#include"textmessagecodec.h"
#include"packetcodec.h"
#include"onlineclientlistcodec.h"
#include <algorithm>
#include"filetransfercontrolcodec.h"
#include "filechunkcodec.h"
#include<QSet>

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

        socket->setReadBufferSize(SOCKET_READ_BUFFER_LIMIT);

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
            cleanupTransfersForClient(clientId);
            m_sessions.remove(clientId);
            emit clientCountChanged(m_sessions.size());
            emit logMessage(QString("Client %1 disconnected").arg(clientId));
            broadcastOnlineClientList();
            socket->deleteLater();
        });
        connect(socket,&QTcpSocket::errorOccurred,
                this,[this,socket,clientId](
                    QAbstractSocket::SocketError){

                    emit logMessage(
                        QString("Client %1 socket error: %2")
                            .arg(clientId)
                            .arg(socket->errorString()));
                });

        connect(socket,&QTcpSocket::bytesWritten,
            this,[this,clientId](qint64){
                resumeSendersForReceiver(
                    clientId);
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

    // 下游拥塞时，不再把数据从 QTcpSocket 搬进应用层缓冲。
    if(session.readPaused){
        return;
    }

    quint32 blockedReceiverId=0;
    if(findBackpressuredReceiver(clientId,blockedReceiverId)){
        session.readPaused=true;
        session.blockedReceiverId=blockedReceiverId;
        return;
    }

    QByteArray &receiveBuffer=session.receiveBuffer;
    QByteArray newData=socket->readAll();
    receiveBuffer.append(newData);

    while(true){
        // 上一个 Chunk 可能刚刚让 Receiver 达到高水位。
        // 必须在消费下一个 Packet 前再次检查。
        blockedReceiverId=0;
        if(findBackpressuredReceiver(clientId,blockedReceiverId)){
            session.readPaused=true;
            session.blockedReceiverId=blockedReceiverId;
            return;
        }
        Protocol::Packet packet;
        Protocol::ParseResult result=Protocol::PacketParser::tryParse(
            receiveBuffer,packet);
        if(result==Protocol::ParseResult::NeedMoreData){
            break;
        }
        if(result==Protocol::ParseResult::InvalidData){
            emit logMessage("Received invalid QtLink packet");
            receiveBuffer.clear();
            socket->disconnectFromHost();
            break;
        }

        switch (packet.type) {
        case Protocol::MessageType::TextSendRequest:
            handleTextSendRequest(clientId,packet.body);
            break;
        case Protocol::MessageType::FileStartRequest:
            handleFileStartRequest(clientId,packet.body);
            break;
        case Protocol::MessageType::FileChunkRequest:
            handleFileChunkRequest(clientId,packet.body);
            break;
        case Protocol::MessageType::FileEndRequest:
            handleFileEndRequest(clientId,packet.body);
            break;
        default:
            emit logMessage(QString("Client %1 sent an unexpected message type.").arg(clientId));
            receiveBuffer.clear();
            socket->disconnectFromHost();
            return;
        }
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

void TcpServer::handleFileStartRequest(quint32 senderId, const QByteArray &body)
{
    quint32 receiverId=0;
    QString transferId;
    QString fileName;
    qint64 fileSize=0;

    if(!Protocol::FileTransferControlCodec::decodeStartRequest(
            body,receiverId,transferId,fileName,fileSize)){
        return;
    }

    if(senderId==receiverId){
        return;
    }

    auto receiverIt=m_sessions.find(receiverId);
    if(receiverIt==m_sessions.end()
        ||receiverIt->socket==nullptr
        ||receiverIt->socket->state()!=QAbstractSocket::ConnectedState){
        return;
    }

    if(m_activeTransfers.contains(transferId)){
        return;
    }

    QByteArray startBody=Protocol::FileTransferControlCodec::encodeStart(
        senderId,transferId,fileName,fileSize);

    if(startBody.isEmpty()){
        return;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileStart;
    packet.body=startBody;

    QByteArray encoded=Protocol::PacketCodec::encode(packet);

    if(receiverIt->socket->write(encoded)<0){
        return;
    }

    ActiveTransfer transfer;
    transfer.senderId=senderId;
    transfer.receiverId=receiverId;
    transfer.fileSize=fileSize;
    transfer.nextOffset=0;

    m_activeTransfers.insert(transferId,transfer);

    emit logMessage(QString("File transfer %1 started: Client %2 ->Client %3")
                        .arg(transferId).arg(senderId).arg(receiverId));
}

void TcpServer::handleTextSendRequest(quint32 senderId, const QByteArray &body)
{
    quint32 receiverId;
    QString content;
    if(!Protocol::TextMessageCodec::decodeSendRequest(body,receiverId,content)){
        emit logMessage(QString("Client%1 sent an invalid text request").arg(senderId));
        return;
    }

    if(receiverId==senderId){
        emit logMessage(
            QString("Client %1 attempted to send a message to itself")
                .arg(senderId));
        return;
    }

    auto receiverIt=m_sessions.find(receiverId);

    if(receiverIt==m_sessions.end()){
        emit logMessage(
            QString("Target client %1 is not connected")
                .arg(receiverId));
        return;
    }

    QTcpSocket*receiverSocket=receiverIt.value().socket;

    if(!receiverSocket
        || receiverSocket->state()!=QAbstractSocket::ConnectedState){

        emit logMessage(
            QString("Target client %1 is not connected")
                .arg(receiverId));
        return;
    }

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
        return;
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
        return;
    }

    emit logMessage(
        QString("Client %1 -> Client %2: %3")
            .arg(senderId)
            .arg(receiverId)
            .arg(content));
}

void TcpServer::handleFileChunkRequest(quint32 senderId, const QByteArray &body)
{
    QString transferId;
    qint64 offset=0;
    QByteArray chunkData;

    if(!Protocol::FileChunkCodec::decode(body,transferId,offset,chunkData)){
        emit logMessage(QString("Client %1 sent an invalid file chunk").arg(senderId));
        return;
    }

    const qint64 chunkSize=static_cast<qint64>(chunkData.size());
    if(chunkSize<=0||chunkSize>Protocol::MAX_FILE_CHUNK_DATA_SIZE){
        emit logMessage(QString("Invalid file chunk size from client %1: %2")
                .arg(senderId).arg(chunkSize));
        return;
    }

    auto transferIt=m_activeTransfers.find(transferId);
    if(transferIt==m_activeTransfers.end()){
        emit logMessage(QString("Unknown file transfer %1").arg(transferId));
        return;
    }

    ActiveTransfer &transfer=transferIt.value();
    if(transfer.senderId!=senderId){
        emit logMessage(QString("Client %1 is not the sender of transfer %2")
                .arg(senderId)
                .arg(transferId));
        return;
    }

    if(offset!=transfer.nextOffset){
        emit logMessage(QString("Unexpected offset for transfer %1")
                .arg(transferId));
        return;
    }

    if(transfer.nextOffset>transfer.fileSize
        ||chunkSize>transfer.fileSize-transfer.nextOffset){
        emit logMessage(QString("File chunk exceeds file size for transfer %1")
                .arg(transferId));
        return;
    }

    auto receiverIt=m_sessions.find(transfer.receiverId);
    if(receiverIt==m_sessions.end()||receiverIt.value().socket==nullptr
        ||receiverIt.value().socket->state()!=QAbstractSocket::ConnectedState){
        emit logMessage(QString("Receiver disconnected for transfer %1")
                .arg(transferId));
        return;
    }

    QByteArray chunkBody=Protocol::FileChunkCodec::encode(transferId,offset,chunkData);
    if(chunkBody.isEmpty()){
        return;
    }
    Protocol::Packet packet;
    packet.body=chunkBody;
    packet.type=Protocol::MessageType::FileChunk;
    QByteArray data=Protocol::PacketCodec::encode(packet);
    if(receiverIt.value().socket->write(data)<0){
        emit logMessage(QString("Failed to forward file chunk for transfer %1")
                .arg(transferId));
        return;
    }

    transfer.nextOffset+=chunkSize;
}

void TcpServer::handleFileEndRequest(quint32 senderId, const QByteArray &body)
{
    QString transferId;

    if(!Protocol::FileTransferControlCodec::decodeEnd(body,transferId)){
        emit logMessage(QString("Client %1 sent an invalid file end request")
                .arg(senderId));
        return;
    }

    auto transferIt=m_activeTransfers.find(transferId);

    if(transferIt==m_activeTransfers.end()){
        emit logMessage(QString("Unknown file transfer %1")
                .arg(transferId));
        return;
    }

    const ActiveTransfer&transfer=transferIt.value();

    if(transfer.senderId!=senderId){
        emit logMessage(QString("Client %1 is not the sender of transfer %2")
                .arg(senderId).arg(transferId));
        return;
    }

    if(transfer.nextOffset!=transfer.fileSize){
        emit logMessage(QString("Incomplete file transfer %1: %2/%3 bytes")
                .arg(transferId)
                .arg(transfer.nextOffset)
                .arg(transfer.fileSize));
        return;
    }

    auto receiverIt=m_sessions.find(transfer.receiverId);

    if(receiverIt==m_sessions.end()
        ||receiverIt.value().socket==nullptr
        ||receiverIt.value().socket->state()!=QAbstractSocket::ConnectedState){
        emit logMessage(QString("Receiver disconnected for transfer %1")
                .arg(transferId));
        return;
    }

    QByteArray endBody=Protocol::FileTransferControlCodec::encodeEnd(transferId);

    if(endBody.isEmpty()){
        emit logMessage(QString("Failed to encode file end for transfer %1")
                .arg(transferId));
        return;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileEnd;
    packet.body=endBody;
    QByteArray data=Protocol::PacketCodec::encode(packet);

    QTcpSocket*receiverSocket=receiverIt.value().socket;
    if(receiverSocket->write(data)<0){
        emit logMessage(QString("Failed to forward file end for transfer %1: %2")
                .arg(transferId)
                .arg(receiverSocket->errorString()));
        return;
    }

    m_activeTransfers.erase(transferIt);
    emit logMessage(QString("File transfer %1 data phase completed")
            .arg(transferId));
}

void TcpServer::cleanupTransfersForClient(quint32 clientId)
{
    QSet<quint32> sendersToResume;

    for(auto sessionIt=m_sessions.begin();
         sessionIt!=m_sessions.end();
         ++sessionIt){

        ClientSession &session=sessionIt.value();
        if(session.readPaused
            &&session.blockedReceiverId==clientId){

            session.readPaused=false;
            session.blockedReceiverId=0;
            if(sessionIt.key()!=clientId){
                sendersToResume.insert(sessionIt.key());
            }
        }
    }

    auto it=m_activeTransfers.begin();

    while(it!=m_activeTransfers.end()){

        const ActiveTransfer &transfer=it.value();

        if(transfer.receiverId==clientId
            ||transfer.senderId==clientId){

            if(transfer.receiverId==clientId
                &&transfer.senderId!=clientId){

                sendersToResume.insert(transfer.senderId);
            }

            const QString transferId=it.key();

            emit logMessage(
                QString(
                    "Removing interrupted file transfer %1 "
                    "because client %2 disconnected")
                    .arg(transferId)
                    .arg(clientId));

            it=m_activeTransfers.erase(it);

        }else{
            ++it;
        }
    }

    for(quint32 senderId:sendersToResume){

        handleClientReadyRead(senderId);
    }
}

bool TcpServer::findBackpressuredReceiver(
    quint32 senderId,quint32 &receiverId) const
{
    for(auto transferIt=m_activeTransfers.cbegin();
         transferIt!=m_activeTransfers.cend();
         ++transferIt){

        const ActiveTransfer &transfer=transferIt.value();

        if(transfer.senderId!=senderId){
            continue;
        }

        auto receiverIt=m_sessions.constFind(transfer.receiverId);

        if(receiverIt==m_sessions.cend()){
            continue;
        }

        QTcpSocket *receiverSocket=receiverIt.value().socket;

        if(receiverSocket==nullptr
            ||receiverSocket->state()
                   !=QAbstractSocket::ConnectedState){
            continue;
        }

        if(receiverSocket->bytesToWrite()
            >=FORWARD_HIGH_WATER_MARK){

            receiverId=transfer.receiverId;
            return true;
        }
    }

    return false;
}

void TcpServer::resumeSendersForReceiver(quint32 receiverId)
{
    auto receiverIt=m_sessions.find(receiverId);

    if(receiverIt==m_sessions.end()){
        return;
    }

    QTcpSocket *receiverSocket=receiverIt.value().socket;

    if(receiverSocket==nullptr
        ||receiverSocket->state()
               !=QAbstractSocket::ConnectedState){
        return;
    }

    if(receiverSocket->bytesToWrite()
        >FORWARD_LOW_WATER_MARK){
        return;
    }

    QSet<quint32> senderIds;

    for(auto sessionIt=m_sessions.cbegin();
         sessionIt!=m_sessions.cend();
         ++sessionIt){

        const ClientSession &session=sessionIt.value();
        if(session.readPaused
            &&session.blockedReceiverId==receiverId){

            senderIds.insert(sessionIt.key());
        }
    }

    for(quint32 senderId:senderIds){
        auto senderIt=m_sessions.find(senderId);
        if(senderIt==m_sessions.end()){
            continue;
        }

        senderIt->readPaused=false;
        senderIt->blockedReceiverId=0;
        handleClientReadyRead(senderId);

        if(receiverSocket->bytesToWrite()
            >=FORWARD_HIGH_WATER_MARK){
            break;
        }
    }
}
