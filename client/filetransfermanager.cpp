#include "filetransfermanager.h"
#include<QUuid>
#include<QFileInfo>
#include"connectionManager.h"
#include<QDir>
#include<QFile>
#include"filechunkcodec.h"
#include<QStandardPaths>


FileTransferManager::FileTransferManager(ConnectionManager*connectionManager,QObject *parent)
    :QObject(parent)
    ,m_connectionManager(connectionManager)
{
    connect(m_connectionManager,&ConnectionManager::fileStartReceived,
            this,&FileTransferManager::handleFileStartReceived);
    connect(m_connectionManager,&ConnectionManager::fileChunkReceived,
            this,&FileTransferManager::handleFileChunkReceived);

    connect(m_connectionManager,&ConnectionManager::socketBytesWritten,
            this,&FileTransferManager::handleSocketBytesWritten);

    connect(m_connectionManager,&ConnectionManager::fileEndReceived,
            this,&FileTransferManager::handleFileEndReceived);

    connect(m_connectionManager,&ConnectionManager::fileAbortReceived,
            this,&FileTransferManager::handleFileAbortReceived);

    connect(m_connectionManager,&ConnectionManager::fileCompleteReceived,
            this,&FileTransferManager::handleFileCompleteReceived);

    connect(m_connectionManager,&ConnectionManager::disconnected,
            this,&FileTransferManager::handleConnectionDisconnected);
}

bool FileTransferManager::sendFile(
    const QString &filePath, quint32 receiverId)
{
    if(receiverId==0){
        return false;
    }

    if(m_sendState!=SendState::Idle){
        return false;
    }

    QFileInfo fileInfo(filePath);
    if(!fileInfo.exists()||!fileInfo.isFile()){
        return false;
    }

    m_sendFile.setFileName(filePath);

    if(!m_sendFile.open(QIODevice::ReadOnly)){
        return false;
    }

    QString transferId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    qint64 fileSize=m_sendFile.size();

    m_sendTransferId=transferId;
    m_sendFileSize=fileSize;
    m_sendOffset=0;
    m_sendState=SendState::Starting;

    if(!m_connectionManager->sendFileStartRequest(
            receiverId,transferId,fileInfo.fileName(),fileSize)){
        resetSendTransfer();
        return false;
    }

    emit sendProgressChanged(0,m_sendFileSize);

    if(m_sendFileSize==0){
        return finishSendTransfer();
    }

    m_sendState=SendState::Sending;
    return pumpSendQueue();
}

QFile *FileTransferManager::createReceiveFile(const QString &fileName,Protocol::FileAbortReason &failureReason)
{
    failureReason=Protocol::FileAbortReason::IoError;

    QString downloadPath=QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if(downloadPath.isEmpty()){
        return nullptr;
    }

    QDir downloadDir(downloadPath);
    if(!downloadDir.mkpath("QtLink")){
        return nullptr;
    }

    QDir receiveDir(downloadDir.filePath("QtLink"));

    QString normalizedFileName=fileName;
    normalizedFileName.replace('\\','/');
    QString safeFileName=QFileInfo(normalizedFileName).fileName();
    if(safeFileName.isEmpty()
        ||safeFileName==QStringLiteral(".")
        ||safeFileName==QStringLiteral("..")){
        failureReason=Protocol::FileAbortReason::ProtocolError;
        return nullptr;
    }

    QFileInfo fileInfo(safeFileName);
    QString baseName=fileInfo.completeBaseName();
    QString suffix=fileInfo.completeSuffix();

    constexpr int maximumAttempts=10000;
    for(int index=0;index<maximumAttempts;++index){
        QString candidateFileName=safeFileName;
        if(index>0){
            if(suffix.isEmpty()){
                candidateFileName=QString("%1 (%2)").arg(baseName).arg(index);
            }else{
                candidateFileName=
                    QString("%1 (%2).%3")
                        .arg(baseName)
                        .arg(index)
                        .arg(suffix);
            }
        }

        const QString filePath=receiveDir.filePath(candidateFileName);
        QFile *file=new QFile(filePath,this);
        if(file->open(QIODevice::WriteOnly|QIODevice::NewOnly)){
            return file;
        }

        delete file;
        if(!QFileInfo::exists(filePath)){
            return nullptr;
        }
    }

    return nullptr;
}

bool FileTransferManager::sendNextChunk()
{
    if(m_sendState!=SendState::Sending){
        return false;
    }

    if(!m_sendFile.isOpen()){
        abortOutgoingTransfer(Protocol::FileAbortReason::IoError);
        return false;
    }

    const qint64 remaining=m_sendFileSize-m_sendOffset;
    if(remaining<=0){
        return false;
    }
    const qint64 bytesToRead=qMin(Protocol::MAX_FILE_CHUNK_DATA_SIZE,remaining);
    QByteArray chunkData=m_sendFile.read(bytesToRead);

    if(static_cast<qint64>(chunkData.size())!=bytesToRead){
        abortOutgoingTransfer(Protocol::FileAbortReason::IoError);
        return false;
    }

    if(!m_connectionManager->sendFileChunkRequest(
            m_sendTransferId,m_sendOffset,chunkData)){
        abortOutgoingTransfer(Protocol::FileAbortReason::PeerDisconnected);
        return false;
    }

    m_sendOffset+=chunkData.size();

    emit sendProgressChanged(m_sendOffset,m_sendFileSize);

    return true;
}

bool FileTransferManager::pumpSendQueue()
{
    if(m_sendState!=SendState::Sending){
        return false;
    }

    if(!m_sendFile.isOpen()){
        abortOutgoingTransfer(Protocol::FileAbortReason::IoError);
        return false;
    }

    while(m_sendOffset<m_sendFileSize
           &&m_connectionManager->pendingBytesToWrite()<SEND_HIGH_WATER_MARK){
        if(!sendNextChunk()){
            return false;
        }
    }

    if(m_sendOffset==m_sendFileSize){
        return finishSendTransfer();
    }

    return true;
}

bool FileTransferManager::finishSendTransfer()
{
    if(m_sendState!=SendState::Sending&&m_sendState!=SendState::Starting){
        return false;
    }

    if(m_sendOffset!=m_sendFileSize){
        abortOutgoingTransfer(Protocol::FileAbortReason::ProtocolError);
        return false;
    }

    if(m_sendFile.isOpen()){
        m_sendFile.close();
    }

    m_sendState=SendState::Finishing;

    if(!m_connectionManager->sendFileEndRequest(m_sendTransferId)){
        abortOutgoingTransfer(Protocol::FileAbortReason::PeerDisconnected);
        return false;
    }

    return true;

}

void FileTransferManager::resetSendTransfer()
{
    if(m_sendFile.isOpen()){
        m_sendFile.close();
    }
    m_sendTransferId.clear();
    m_sendFileSize=0;
    m_sendOffset=0;
    m_sendState=SendState::Idle;
}

void FileTransferManager::abortOutgoingTransfer(Protocol::FileAbortReason reason)
{
    if(m_sendState==SendState::Idle||m_sendTransferId.isEmpty()){
        return;
    }

    const QString transferId=m_sendTransferId;

    /*
     * 先结束本地发送状态。
     * 即使 AbortRequest 发送失败，也不能继续使用已经失败的任务。
     */
    resetSendTransfer();

    if(!m_connectionManager->sendFileAbortRequest(transferId,reason)){
        m_connectionManager->abortConnectionForReconnect();
    }
}

void FileTransferManager::cleanupIncomingTransfer(const QString &transferId)
{
    auto transferIt=m_incomingTransfers.find(transferId);

    if(transferIt==m_incomingTransfers.end()){
        return;
    }

    QFile *file=transferIt.value().file;

    QString filePath;

    if(file!=nullptr){
        filePath=file->fileName();
    }

    /*
     * 先移除业务状态，
     * 防止后续到达的旧 Chunk 继续使用这次接收任务。
     */
    m_incomingTransfers.erase(transferIt);

    if(file==nullptr){
        return;
    }

    if(file->isOpen()){
        file->close();
    }

    file->deleteLater();

    if(!filePath.isEmpty()){
        QFile::remove(filePath);
    }
}

void FileTransferManager::abortIncomingTransfer(const QString &transferId, Protocol::FileAbortReason reason)
{
    cleanupIncomingTransfer(transferId);

    if(!m_connectionManager->sendFileAbortRequest(transferId,reason)){
        m_connectionManager->abortConnectionForReconnect();
    }
}

void FileTransferManager::handleFileStartReceived(
    quint32 senderId, const QString &transferId, const QString &fileName, qint64 fileSize)
{
    if(m_incomingTransfers.contains(transferId)){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    Protocol::FileAbortReason failureReason=Protocol::FileAbortReason::IoError;
    QFile *file=createReceiveFile(fileName,failureReason);
    if(file==nullptr){
        if(!m_connectionManager->sendFileAbortRequest(transferId,failureReason)){
            m_connectionManager->abortConnectionForReconnect();
        }
        return;
    }

    IncomingTransfer transfer;
    transfer.file=file;
    transfer.fileSize=fileSize;
    transfer.nextOffset=0;
    transfer.senderId=senderId;

    m_incomingTransfers.insert(transferId,transfer);
}

void FileTransferManager::handleFileChunkReceived(
    const QString &transferId, qint64 offset, const QByteArray &chunkData)
{
    auto transferIt=m_incomingTransfers.find(transferId);
    if(transferIt==m_incomingTransfers.end()){
        return;
    }

    IncomingTransfer &transfer=transferIt.value();
    if(offset!=transfer.nextOffset){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    const qint64 chunkSize=static_cast<qint64>(chunkData.size());

    if(transfer.nextOffset>transfer.fileSize
        ||chunkSize>transfer.fileSize-transfer.nextOffset){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    if(transfer.file==nullptr||!transfer.file->isOpen()){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::IoError);
        return;
    }

    qint64 totalWritten=0;
    while(totalWritten<chunkSize){
        const qint64 written=transfer.file->write(chunkData.constData()+totalWritten,
                                                    chunkSize-totalWritten);
        if(written<=0){
            abortIncomingTransfer(transferId,Protocol::FileAbortReason::IoError);
            return;
        }
        totalWritten+=written;
    }

    transfer.nextOffset+=totalWritten;
}

void FileTransferManager::handleSocketBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    if(m_sendState==SendState::Sending){
        pumpSendQueue();
    }
}

void FileTransferManager::handleFileEndReceived(const QString &transferId)
{
    auto transferIt=m_incomingTransfers.find(transferId);
    if(transferIt==m_incomingTransfers.end()){
        return;
    }

    IncomingTransfer&transfer=transferIt.value();

    QFile*file=transfer.file;
    if(file==nullptr){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::IoError);
        return;
    }

    const QString filePath=file->fileName();

    if(transfer.nextOffset!=transfer.fileSize){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::ProtocolError);
        return;
    }

    if(!file->flush()){
        abortIncomingTransfer(transferId,Protocol::FileAbortReason::IoError);
        return;
    }

    file->close();
    file->deleteLater();
    m_incomingTransfers.erase(transferIt);
    emit fileReceiveCompleted(filePath);

    if(!m_connectionManager->sendFileCompleteRequest(transferId)){
        m_connectionManager->abortConnectionForReconnect();
    }
}

void FileTransferManager::handleFileCompleteReceived(const QString &transferId)
{
    if(m_sendState==SendState::Finishing
        &&m_sendTransferId==transferId){

        resetSendTransfer();
    }
}

void FileTransferManager::handleFileAbortReceived(const QString &transferId, Protocol::FileAbortReason reason)
{
    Q_UNUSED(reason);

    /*
     * 当前 Client 是该 transfer 的 Sender。
     */
    if(m_sendState!=SendState::Idle&&m_sendTransferId==transferId){
        resetSendTransfer();
        return;
    }

    /*
     * 当前 Client 是该 transfer 的 Receiver。
     */
    if(m_incomingTransfers.contains(transferId)){

        cleanupIncomingTransfer(transferId);
    }
}


void FileTransferManager::handleConnectionDisconnected()
{
    resetSendTransfer();
    for(auto it=m_incomingTransfers.begin();it!=m_incomingTransfers.end();it++){
        QFile*file=it.value().file;
        if(file==nullptr){
            continue;
        }
        const QString filePath=file->fileName();
        if(file->isOpen()){
            file->close();
        }
        delete file;
        QFile::remove(filePath);
    }
    m_incomingTransfers.clear();
}



