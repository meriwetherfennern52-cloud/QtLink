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
#include<QTimer>

TcpServer::TcpServer(QObject *parent)
    : QObject{parent}
    ,m_server(new QTcpServer(this))
    ,m_transferWatchTimer(new QTimer(this))
    ,m_clientWatchTimer(new QTimer(this))
{
    connect(m_server,&QTcpServer::newConnection,this,&TcpServer::handleNewConnection);

    connect(m_transferWatchTimer,&QTimer::timeout,
            this,&TcpServer::checkStalledTransfers);
    connect(m_clientWatchTimer,&QTimer::timeout,
            this,&TcpServer::checkClientTimeouts);
    m_activityClock.start();
    m_transferWatchTimer->start(TRANSFER_WATCH_INTERVAL_MS);
    m_clientWatchTimer->start(CLIENT_WATCH_INTERVAL_MS);
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
        session.lastActivityMs=m_activityClock.elapsed();
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
                updateReceiverTransferActivity(clientId);
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
        if(session.readPaused){
            return;
        }

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

        session.lastActivityMs=m_activityClock.elapsed();

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
        case Protocol::MessageType::FileAbortRequest:
            handleFileAbortRequest(clientId,packet.body);
            break;
        case Protocol::MessageType::FileCompleteRequest:
            handleFileCompleteRequest(clientId,packet.body);
            break;
        case Protocol::MessageType::HeartbeatRequest:
            handleHeartbeatRequest(clientId,packet.body);
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

    for(auto it=m_sessions.cbegin();it!=m_sessions.cend();it++){

        QTcpSocket*socket=it.value().socket;

        if(socket&& socket->state()==
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
        emit logMessage(QString("Client %1 sent an invalid file start request")
                .arg(senderId));
        scheduleClientDisconnect(senderId);
        return;
    }

    if(senderId==receiverId){
        sendFileAbort(senderId,transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    auto receiverIt=m_sessions.find(receiverId);
    if(receiverIt==m_sessions.end()
        ||receiverIt->socket==nullptr
        ||receiverIt->socket->state()!=QAbstractSocket::ConnectedState){
        sendFileAbort(senderId,transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    if(m_activeTransfers.contains(transferId)){
        sendFileAbort(senderId,transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    QByteArray startBody=Protocol::FileTransferControlCodec::encodeStart(
        senderId,transferId,fileName,fileSize);

    if(startBody.isEmpty()){
        sendFileAbort(senderId,transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileStart;
    packet.body=startBody;

    QByteArray encoded=Protocol::PacketCodec::encode(packet);

    if(receiverIt->socket->write(encoded)<0){
        sendFileAbort(senderId,transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    ActiveTransfer transfer;
    transfer.senderId=senderId;
    transfer.receiverId=receiverId;
    transfer.fileSize=fileSize;
    transfer.nextOffset=0;
    transfer.phase=TransferPhase::Transferring;
    transfer.lastActivityMs=m_activityClock.elapsed();

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
        scheduleClientDisconnect(senderId);
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
        Protocol::TextMessageCodec::encodeTextMessage(senderId,content);

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
        QString("Text message forwarded: Client %1 -> Client %2")
            .arg(senderId)
            .arg(receiverId));
}

void TcpServer::handleFileChunkRequest(quint32 senderId, const QByteArray &body)
{
    QString transferId;
    qint64 offset=0;
    QByteArray chunkData;

    if(!Protocol::FileChunkCodec::decode(body,transferId,offset,chunkData)){
        emit logMessage(QString("Client %1 sent an invalid file chunk").arg(senderId));
        scheduleClientDisconnect(senderId);
        return;
    }

    const qint64 chunkSize=static_cast<qint64>(chunkData.size());
    if(chunkSize<=0||chunkSize>Protocol::MAX_FILE_CHUNK_DATA_SIZE){
        emit logMessage(QString("Invalid file chunk size from client %1: %2")
                .arg(senderId).arg(chunkSize));
        scheduleClientDisconnect(senderId);
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

    if(transfer.phase!=TransferPhase::Transferring){

        emit logMessage(QString(
                "File chunk received after data phase ended "
                "for transfer %1")
                .arg(transferId));

        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);

        return;
    }

    if(offset!=transfer.nextOffset){
        emit logMessage(QString("Unexpected offset for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    if(transfer.nextOffset>transfer.fileSize
        ||chunkSize>transfer.fileSize-transfer.nextOffset){
        emit logMessage(QString("File chunk exceeds file size for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    auto receiverIt=m_sessions.find(transfer.receiverId);
    if(receiverIt==m_sessions.end()||receiverIt.value().socket==nullptr
        ||receiverIt.value().socket->state()!=QAbstractSocket::ConnectedState){
        emit logMessage(QString("Receiver disconnected for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    QByteArray chunkBody=Protocol::FileChunkCodec::encode(transferId,offset,chunkData);
    if(chunkBody.isEmpty()){
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }
    Protocol::Packet packet;
    packet.body=chunkBody;
    packet.type=Protocol::MessageType::FileChunk;
    QByteArray data=Protocol::PacketCodec::encode(packet);
    if(receiverIt.value().socket->write(data)<0){
        emit logMessage(QString("Failed to forward file chunk for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    transfer.nextOffset+=chunkSize;
    transfer.lastActivityMs=m_activityClock.elapsed();
}

void TcpServer::handleFileEndRequest(quint32 senderId, const QByteArray &body)
{
    QString transferId;

    if(!Protocol::FileTransferControlCodec::decodeEnd(body,transferId)){
        emit logMessage(QString("Client %1 sent an invalid file end request")
                .arg(senderId));
        scheduleClientDisconnect(senderId);
        return;
    }

    auto transferIt=m_activeTransfers.find(transferId);

    if(transferIt==m_activeTransfers.end()){
        emit logMessage(QString("Unknown file transfer %1")
                .arg(transferId));
        return;
    }

    ActiveTransfer&transfer=transferIt.value();

    if(transfer.senderId!=senderId){
        emit logMessage(QString("Client %1 is not the sender of transfer %2")
                .arg(senderId).arg(transferId));
        return;
    }

    if(transfer.phase!=TransferPhase::Transferring){

        emit logMessage(QString(
                "File end received after data phase ended "
                "for transfer %1")
                .arg(transferId));

        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);

        return;
    }

    if(transfer.nextOffset!=transfer.fileSize){
        emit logMessage(QString("Incomplete file transfer %1: %2/%3 bytes")
                .arg(transferId)
                .arg(transfer.nextOffset)
                .arg(transfer.fileSize));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    auto receiverIt=m_sessions.find(transfer.receiverId);

    if(receiverIt==m_sessions.end()
        ||receiverIt.value().socket==nullptr
        ||receiverIt.value().socket->state()!=QAbstractSocket::ConnectedState){
        emit logMessage(QString("Receiver disconnected for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    QByteArray endBody=Protocol::FileTransferControlCodec::encodeEnd(transferId);

    if(endBody.isEmpty()){
        emit logMessage(QString("Failed to encode file end for transfer %1")
                .arg(transferId));
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
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
        abortTransferFromServer(transferId,Protocol::FileAbortReason::PeerDisconnected);
        return;
    }

    transfer.phase=TransferPhase::AwaitingReceiverComplete;
    transfer.lastActivityMs=m_activityClock.elapsed();

    emit logMessage(QString("File transfer %1 data phase completed")
            .arg(transferId));
}

void TcpServer::handleFileCompleteRequest(quint32 receiverId, const QByteArray &body)
{
    QString transferId;
    if(!Protocol::FileTransferControlCodec::decodeComplete(body,transferId)){
        emit logMessage(QString(
                "Client %1 sent an invalid "
                "file complete request")
                .arg(receiverId));

        scheduleClientDisconnect(receiverId);
        return;
    }

    auto transferIt=m_activeTransfers.find(transferId);

    /*
     * Complete 可能晚于 Abort / disconnect 到达。
     * 如果 transfer 已经结束，直接忽略。
     */
    if(transferIt==m_activeTransfers.end()){

        emit logMessage(QString(
                "Unknown file transfer %1 "
                "in complete request")
                .arg(transferId));

        return;
    }

    const ActiveTransfer transfer=transferIt.value();

    /*
     * 只有真正的 Receiver 才有资格声明文件已经保存成功。
     */
    if(receiverId!=transfer.receiverId){
        emit logMessage(QString(
                "Client %1 is not the receiver "
                "of transfer %2")
                .arg(receiverId).arg(transferId));

        return;
    }

    /*
     * Receiver 只能在 Server 已经转发 FileEnd 后声明完成。
     */
    if(transfer.phase!=TransferPhase::AwaitingReceiverComplete){
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    /*
     * Server 自己记录的数据长度也必须完整。
     */
    if(transfer.nextOffset!=transfer.fileSize){
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    auto senderIt=m_sessions.find(transfer.senderId);

    /*
     * Sender 已经不在线，则没有继续保留 AwaitingReceiverComplete 的意义。
     */
    if(senderIt==m_sessions.end()||senderIt.value().socket==nullptr||senderIt.value().socket->state()
               !=QAbstractSocket::ConnectedState){

        m_activeTransfers.erase(transferIt);
        resumeSenderAfterTransferRemoval(transfer.senderId,transfer.receiverId);

        return;
    }

    const QByteArray completeBody=Protocol::FileTransferControlCodec::encodeComplete(transferId);

    if(completeBody.isEmpty()){
        abortTransferFromServer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileComplete;
    packet.body=completeBody;

    const QByteArray data=Protocol::PacketCodec::encode(packet);

    QTcpSocket *senderSocket=senderIt.value().socket;

    if(senderSocket->write(data)<0){
        /*
         * Receiver 已经成功保存文件，但 Sender 已无法收到完成通知。
         *
         * Server 不应继续永久保存该 transfer。
         */
        m_activeTransfers.erase(transferIt);
        resumeSenderAfterTransferRemoval(transfer.senderId,transfer.receiverId);
        scheduleClientDisconnect(transfer.senderId);

        return;
    }

    /*
     * FileComplete 已成功进入 Sender 的 Socket 写缓冲，
     * Server 到这里才正式结束该 transfer。
     */
    m_activeTransfers.erase(transferIt);
    resumeSenderAfterTransferRemoval(transfer.senderId,transfer.receiverId);

    emit logMessage(QString(
            "File transfer %1 completed")
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
            session.lastActivityMs=m_activityClock.elapsed();
            if(sessionIt.key()!=clientId){
                sendersToResume.insert(sessionIt.key());
            }
        }
    }

    auto it=m_activeTransfers.begin();

    while(it!=m_activeTransfers.end()){

        const ActiveTransfer transfer=it.value();

        if(transfer.receiverId==clientId
            ||transfer.senderId==clientId){

            if(transfer.receiverId==clientId
                &&transfer.senderId!=clientId){

                sendersToResume.insert(transfer.senderId);
            }

            const QString transferId=it.key();

            const quint32 peerId=
                transfer.senderId==clientId?transfer.receiverId
                                            :transfer.senderId;

            emit logMessage(
                QString(
                    "Removing interrupted file transfer %1 "
                    "because client %2 disconnected")
                    .arg(transferId)
                    .arg(clientId));

            it=m_activeTransfers.erase(it);

            sendFileAbort(peerId,transferId,
                Protocol::FileAbortReason::PeerDisconnected);

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

        if(transfer.phase!=TransferPhase::Transferring){
            continue;
        }

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

bool TcpServer::hasTransferringDependency(
    quint32 senderId,quint32 receiverId) const
{
    for(auto transferIt=m_activeTransfers.cbegin();
         transferIt!=m_activeTransfers.cend();
         ++transferIt){

        const ActiveTransfer &transfer=transferIt.value();
        if(transfer.phase==TransferPhase::Transferring
            &&transfer.senderId==senderId
            &&transfer.receiverId==receiverId){

            return true;
        }
    }

    return false;
}

void TcpServer::resumeSenderAfterTransferRemoval(
    quint32 senderId,quint32 receiverId)
{
    auto senderIt=m_sessions.find(senderId);
    if(senderIt==m_sessions.end()
        ||!senderIt.value().readPaused
        ||senderIt.value().blockedReceiverId!=receiverId
        ||hasTransferringDependency(senderId,receiverId)){

        return;
    }

    senderIt.value().readPaused=false;
    senderIt.value().blockedReceiverId=0;
    senderIt.value().lastActivityMs=m_activityClock.elapsed();
    handleClientReadyRead(senderId);
}

void TcpServer::scheduleClientDisconnect(quint32 clientId)
{
    auto clientIt=m_sessions.find(clientId);
    if(clientIt==m_sessions.end()||clientIt.value().socket==nullptr){
        return;
    }

    QTcpSocket *socket=clientIt.value().socket;
    clientIt.value().readPaused=true;
    QMetaObject::invokeMethod(socket,[socket](){
        socket->abort();
    },Qt::QueuedConnection);
}

void TcpServer::updateReceiverTransferActivity(quint32 receiverId)
{
    const qint64 now=m_activityClock.elapsed();
    for(auto transferIt=m_activeTransfers.begin();
         transferIt!=m_activeTransfers.end();
         ++transferIt){

        if(transferIt.value().receiverId==receiverId
            &&transferIt.value().phase==TransferPhase::Transferring){
            transferIt.value().lastActivityMs=now;
        }
    }
}

void TcpServer::checkStalledTransfers()
{
    const qint64 now=m_activityClock.elapsed();
    QStringList stalledTransferIds;

    for(auto transferIt=m_activeTransfers.cbegin();
         transferIt!=m_activeTransfers.cend();
         ++transferIt){

        if(now-transferIt.value().lastActivityMs
            >=TRANSFER_STALL_TIMEOUT_MS){

            stalledTransferIds.append(transferIt.key());
        }
    }

    for(const QString &transferId:stalledTransferIds){
        emit logMessage(QString("File transfer %1 timed out")
                .arg(transferId));
        abortTransferFromServer(
            transferId,Protocol::FileAbortReason::Timeout);
    }
}

void TcpServer::handleHeartbeatRequest(
    quint32 clientId,const QByteArray &body)
{
    if(!body.isEmpty()){
        emit logMessage(QString("Client %1 sent an invalid heartbeat request")
                .arg(clientId));
        scheduleClientDisconnect(clientId);
        return;
    }

    auto clientIt=m_sessions.find(clientId);
    if(clientIt==m_sessions.end()||clientIt.value().socket==nullptr
        ||clientIt.value().socket->state()!=QAbstractSocket::ConnectedState){

        return;
    }

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::HeartbeatResponse;
    packet.body.clear();

    const QByteArray data=Protocol::PacketCodec::encode(packet);
    if(clientIt.value().socket->write(data)<0){
        emit logMessage(QString("Failed to send heartbeat response to Client %1")
                .arg(clientId));
        scheduleClientDisconnect(clientId);
    }
}

void TcpServer::checkClientTimeouts()
{
    const qint64 now=m_activityClock.elapsed();
    QList<quint32> timedOutClientIds;

    for(auto sessionIt=m_sessions.cbegin();
         sessionIt!=m_sessions.cend();
         ++sessionIt){

        const ClientSession &session=sessionIt.value();
        if(session.readPaused){
            continue;
        }

        if(now-session.lastActivityMs>=CLIENT_TIMEOUT_MS){
            timedOutClientIds.append(sessionIt.key());
        }
    }

    for(quint32 clientId:timedOutClientIds){
        emit logMessage(QString("Client %1 heartbeat timed out")
                .arg(clientId));
        scheduleClientDisconnect(clientId);
    }
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
        senderIt->lastActivityMs=m_activityClock.elapsed();
        handleClientReadyRead(senderId);

        if(receiverSocket->bytesToWrite()
            >=FORWARD_HIGH_WATER_MARK){
            break;
        }
    }
}

void TcpServer::handleFileAbortRequest(quint32 clientId, const QByteArray &body)
{
    QString transferId;
    Protocol::FileAbortReason reason=Protocol::FileAbortReason::ProtocolError;

    if(!Protocol::FileTransferControlCodec::decodeAbort(body,transferId,reason)){
        emit logMessage(QString("Client %1 sent an invalid file abort request")
                .arg(clientId));
        scheduleClientDisconnect(clientId);
        return;
    }

    auto transferIt=m_activeTransfers.find(transferId);
    if(transferIt==m_activeTransfers.end()){
        emit logMessage(QString("Unknown file transfer %1 in abort request")
                .arg(transferId));
        return;
    }

    // 后面要 erase iterator，所以先复制出传输信息。
    const ActiveTransfer transfer=transferIt.value();
    if(clientId!=transfer.senderId&&clientId!=transfer.receiverId){
        emit logMessage(QString("Client %1 is not part of transfer %2")
                .arg(clientId).arg(transferId));
        return;
    }

    const quint32 peerId=
        clientId==transfer.senderId?transfer.receiverId:transfer.senderId;
    // Abort 一旦被合法接受，这条 transfer 在 Server 端立即结束。
    m_activeTransfers.erase(transferIt);
    sendFileAbort(peerId,transferId,reason);
    resumeSenderAfterTransferRemoval(transfer.senderId,transfer.receiverId);

    emit logMessage(
        QString("File transfer %1 aborted by Client %2")
            .arg(transferId).arg(clientId));

}

bool TcpServer::sendFileAbort(quint32 clientId,
            const QString &transferId, Protocol::FileAbortReason reason)
{
    auto clientIt=m_sessions.find(clientId);
    if(clientIt==m_sessions.end()){
        return false;
    }

    QTcpSocket*socket=clientIt.value().socket;
    if(socket==nullptr||socket->state()!=QAbstractSocket::ConnectedState){
        return false;
    }

    const QByteArray abortBody=Protocol::FileTransferControlCodec::encodeAbort(transferId,reason);
    if(abortBody.isEmpty()){
        emit logMessage(QString(
                "Failed to encode file abort for transfer %1")
                .arg(transferId));
        return false;
    }

    Protocol::Packet packet;
    packet.body=abortBody;
    packet.type=Protocol::MessageType::FileAbort;

    QByteArray data=Protocol::PacketCodec::encode(packet);
    if(socket->write(data)<0){
        emit logMessage(
            QString("Failed to send file abort for transfer %1 "
                "to Client %2: %3")
                .arg(transferId).arg(clientId)
                .arg(socket->errorString()));
        scheduleClientDisconnect(clientId);
        return false;
    }

    return true;
}

void TcpServer::abortTransferFromServer(const QString &transferId, Protocol::FileAbortReason reason)
{
    auto transferIt=m_activeTransfers.find(transferId);
    if(transferIt==m_activeTransfers.end()){
        return;
    }
    const ActiveTransfer transfer=transferIt.value();
    m_activeTransfers.erase(transferIt);

    sendFileAbort(transfer.senderId,transferId,reason);
    sendFileAbort(transfer.receiverId,transferId,reason);
    resumeSenderAfterTransferRemoval(transfer.senderId,transfer.receiverId);
}
