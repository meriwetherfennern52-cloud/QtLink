#include "filechunkcodec.h"

#include <QIODevice>
#include <QUuid>



QByteArray Protocol::FileChunkCodec::encode(const QString &transferId,
        qint64 offset, const QByteArray &chunkData)
{
    QUuid uuid=QUuid::fromString(transferId);
    if(uuid.isNull()){
        return QByteArray();
    }

    QByteArray body=uuid.toRfc4122();
    QByteArray offsetData;
    QDataStream stream(&offsetData,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream<<offset;
    body.append(offsetData);
    body.append(chunkData);
    return body;
}

bool Protocol::FileChunkCodec::decode(const QByteArray &data,
        QString &transferId, qint64 &offset, QByteArray &chunkData)
{
    constexpr qsizetype metadataSize=24;
    if(data.size()<=metadataSize){
        return false;
    }

    QByteArray uuidData=data.left(16);
    QUuid uuid=QUuid::fromRfc4122(uuidData);
    if(uuid.isNull()){
        return false;
    }
    QString parsedTransferId=uuid.toString(QUuid::WithoutBraces);

    QByteArray offsetData=data.mid(16,8);
    QDataStream stream(offsetData);
    stream.setByteOrder(QDataStream::BigEndian);
    qint64 parsedOffset=0;
    stream>>parsedOffset;
    if(stream.status()!=QDataStream::Ok||parsedOffset<0){
        return false;
    }

    QByteArray parsedChunkData=data.mid(metadataSize);
    if(parsedChunkData.isEmpty()){
        return false;
    }

    transferId=parsedTransferId;
    offset=parsedOffset;
    chunkData=parsedChunkData;

    return true;
}
