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
    pumpSendQueue();

    return true;
}

QFile *FileTransferManager::createReceiveFile(const QString &fileName)
{
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
        return false;
    }

    const qint64 remaining=m_sendFileSize-m_sendOffset;
    if(remaining<=0){
        return false;
    }
    const qint64 bytesToRead=qMin(Protocol::MAX_FILE_CHUNK_DATA_SIZE,remaining);
    QByteArray chunkData=m_sendFile.read(bytesToRead);

    if(static_cast<qint64>(chunkData.size())!=bytesToRead){
        m_sendFile.seek(m_sendOffset);
        return false;
    }

    if(!m_connectionManager->sendFileChunkRequest(
            m_sendTransferId,m_sendOffset,chunkData)){
        m_sendFile.seek(m_sendOffset);
        return false;
    }

    m_sendOffset+=chunkData.size();

    emit sendProgressChanged(m_sendOffset,m_sendFileSize);

    return true;
}

void FileTransferManager::pumpSendQueue()
{
    if(m_sendState!=SendState::Sending){
        return;
    }

    if(!m_sendFile.isOpen()){
        return;
    }

    while(m_sendOffset<m_sendFileSize
           &&m_connectionManager->pendingBytesToWrite()<SEND_HIGH_WATER_MARK){
        if(!sendNextChunk()){
            resetSendTransfer();
            return;
        }
    }

    if(m_sendOffset==m_sendFileSize){
        finishSendTransfer();
    }
}

bool FileTransferManager::finishSendTransfer()
{
    if(m_sendState!=SendState::Sending&&m_sendState!=SendState::Starting){
        return false;
    }

    if(m_sendOffset!=m_sendFileSize){
        return false;
    }

    if(m_sendFile.isOpen()){
        m_sendFile.close();
    }

    m_sendState=SendState::Finishing;

    if(!m_connectionManager->sendFileEndRequest(m_sendTransferId)){
        resetSendTransfer();
        return false;
    }

    if(m_connectionManager->pendingBytesToWrite()==0){
        resetSendTransfer();
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

void FileTransferManager::handleFileStartReceived(
    quint32 senderId, const QString &transferId, const QString &fileName, qint64 fileSize)
{
    if(m_incomingTransfers.contains(transferId)){
        return;
    }

    QFile *file=createReceiveFile(fileName);
    if(file==nullptr){
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
        return;
    }

    const qint64 chunkSize=static_cast<qint64>(chunkData.size());

    if(transfer.nextOffset>transfer.fileSize
        ||chunkSize>transfer.fileSize-transfer.nextOffset){
        return;
    }

    if(transfer.file==nullptr||!transfer.file->isOpen()){
        return;
    }

    qint64 totalWritten=0;
    while(totalWritten<chunkSize){
        const qint64 written=transfer.file->write(chunkData.constData()+totalWritten,
                                                    chunkSize-totalWritten);
        if(written<=0){
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
        return;
    }

    if(m_sendState==SendState::Finishing&&m_connectionManager->pendingBytesToWrite()==0){
        resetSendTransfer();
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
        m_incomingTransfers.erase(transferIt);
        return;
    }

    const QString filePath=file->fileName();

    if(transfer.nextOffset!=transfer.fileSize){
        if(file->isOpen()){
            file->close();
        }
        file->deleteLater();
        m_incomingTransfers.erase(transferIt);
        QFile::remove(filePath);
        return;
    }

    if(!file->flush()){
        file->close();
        file->deleteLater();
        m_incomingTransfers.erase(transferIt);
        QFile::remove(filePath);
        return;
    }

    file->close();
    file->deleteLater();
    m_incomingTransfers.erase(transferIt);
    emit fileReceiveCompleted(filePath);
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



