#include "filetransfercontrolcodec.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include<limits>

namespace{

bool normalizeTransferId(const QString &input,QString &normalized)
{
    QUuid uuid=QUuid::fromString(input);
    if(uuid.isNull()){
        return false;
    }
    normalized=uuid.toString(QUuid::WithoutBraces);
    return true;
}

}

QByteArray Protocol::FileTransferControlCodec::encodeStartRequest(
    quint32 receiverId, const QString &transferId,
        const QString &fileName, qint64 fileSize)
{
    QString normalizedTransferId;
    if(!normalizeTransferId(transferId,normalizedTransferId)){
        return QByteArray();
    }

    QJsonObject jsonObject;
    jsonObject["receiverId"]=static_cast<qint64>(receiverId);
    jsonObject["transferId"]=normalizedTransferId;
    jsonObject["fileName"]=fileName;
    jsonObject["fileSize"]=fileSize;
    return QJsonDocument(jsonObject).toJson(QJsonDocument::Compact);
}

bool Protocol::FileTransferControlCodec::decodeStartRequest(
    const QByteArray &data, quint32 &receiverId, QString &transferId,
    QString &fileName, qint64 &fileSize)
{
    QJsonParseError parseError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }

    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue receiverIdValue=jsonObject.value("receiverId");
    QJsonValue transferIdValue=jsonObject.value("transferId");
    QJsonValue fileNameValue=jsonObject.value("fileName");
    QJsonValue fileSizeValue=jsonObject.value("fileSize");
    if(!receiverIdValue.isDouble()||!transferIdValue.isString()||
        !fileNameValue.isString()||!fileSizeValue.isDouble()){
        return false;
    }

    qint64 parsedReceiverId=receiverIdValue.toInteger(-1);
    QString parsedTransferId;
    QString parsedFileName=fileNameValue.toString();
    qint64 parsedFileSize=fileSizeValue.toInteger(-1);

    if(parsedReceiverId<=0||
        parsedReceiverId>static_cast<qint64>(std::numeric_limits<quint32>::max())){
        return false;
    }
    if(!normalizeTransferId(transferIdValue.toString(),parsedTransferId)||
        parsedFileName.isEmpty()||parsedFileSize<0){
        return false;
    }

    receiverId=static_cast<quint32>(parsedReceiverId);
    transferId=parsedTransferId;
    fileName=parsedFileName;
    fileSize=parsedFileSize;

    return true;
}

QByteArray Protocol::FileTransferControlCodec::encodeStart(
    quint32 senderId, const QString &transferId,
        const QString &fileName, qint64 fileSize)
{
    QString normalizedTransferId;
    if(!normalizeTransferId(transferId,normalizedTransferId)){
        return QByteArray();
    }

    QJsonObject jsonObject;
    jsonObject["senderId"]=static_cast<qint64>(senderId);
    jsonObject["transferId"]=normalizedTransferId;
    jsonObject["fileName"]=fileName;
    jsonObject["fileSize"]=fileSize;
    return QJsonDocument(jsonObject).toJson(QJsonDocument::Compact);
}

bool Protocol::FileTransferControlCodec::decodeStart(
    const QByteArray &data, quint32 &senderId, QString &transferId,
    QString &fileName, qint64 &fileSize)
{
    QJsonParseError parseError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }

    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue senderIdValue=jsonObject.value("senderId");
    QJsonValue transferIdValue=jsonObject.value("transferId");
    QJsonValue fileNameValue=jsonObject.value("fileName");
    QJsonValue fileSizeValue=jsonObject.value("fileSize");
    if(!senderIdValue.isDouble()||!transferIdValue.isString()||
        !fileNameValue.isString()||!fileSizeValue.isDouble()){
        return false;
    }

    qint64 parsedSenderId=senderIdValue.toInteger(-1);
    QString parsedTransferId;
    QString parsedFileName=fileNameValue.toString();
    qint64 parsedFileSize=fileSizeValue.toInteger(-1);

    if(parsedSenderId<=0||
        parsedSenderId>static_cast<qint64>(std::numeric_limits<quint32>::max())){
        return false;
    }
    if(!normalizeTransferId(transferIdValue.toString(),parsedTransferId)||
        parsedFileName.isEmpty()||parsedFileSize<0){
        return false;
    }

    senderId=static_cast<quint32>(parsedSenderId);
    transferId=parsedTransferId;
    fileName=parsedFileName;
    fileSize=parsedFileSize;

    return true;
}

QByteArray Protocol::FileTransferControlCodec::encodeEnd(const QString &transferId)
{
    QString normalizedTransferId;
    if(!normalizeTransferId(transferId,normalizedTransferId)){
        return QByteArray();
    }

    QJsonObject jsonObject;
    jsonObject["transferId"]=normalizedTransferId;
    return QJsonDocument(jsonObject).toJson(QJsonDocument::Compact);
}

bool Protocol::FileTransferControlCodec::decodeEnd(const QByteArray &data, QString &transferId)
{
    QJsonParseError parseError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }

    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue transferIdValue=jsonObject.value("transferId");
    if(!transferIdValue.isString()){
        return false;
    }

    QString parsedTransferId;
    if(!normalizeTransferId(transferIdValue.toString(),parsedTransferId)){
        return false;
    }

    transferId=parsedTransferId;

    return true;
}

QByteArray Protocol::FileTransferControlCodec::encodeAbort(const QString &transferId, FileAbortReason reason)
{
    QString normalizedTransferId;
    if(!normalizeTransferId(transferId,normalizedTransferId)){
        return QByteArray();
    }

    const quint8 reasonValue=static_cast<quint8>(reason);

    if(!isValidFileAbortReason(reasonValue)){
        return QByteArray();
    }

    QJsonObject jsonObject;
    jsonObject["transferId"]=normalizedTransferId;
    jsonObject["reason"]=reasonValue;

    return QJsonDocument(jsonObject).toJson(QJsonDocument::Compact);
}

bool Protocol::FileTransferControlCodec::decodeAbort(const QByteArray &data,
                QString &transferId, FileAbortReason &reason)
{
    QJsonParseError parseError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&parseError);

    if(parseError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }

    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue transferIdValue=jsonObject.value("transferId");
    QJsonValue reasonValue=jsonObject.value("reason");

    if(!transferIdValue.isString()||!reasonValue.isDouble()){
        return false;
    }

    QString parseTransferId;
    if(!normalizeTransferId(transferIdValue.toString(),parseTransferId)){
        return false;
    }

    const qint64 parseReason=reasonValue.toInteger(-1);
    if(parseReason<0
        ||parseReason>static_cast<qint64>(std::numeric_limits<quint8>::max())){
        return false;
    }

    if(!isValidFileAbortReason(static_cast<quint8>(parseReason))){
        return false;
    }

    transferId=parseTransferId;
    reason=static_cast<FileAbortReason>(parseReason);

    return true;
}

QByteArray Protocol::FileTransferControlCodec::encodeComplete(const QString &transferId)
{
    return encodeEnd(transferId);
}

bool Protocol::FileTransferControlCodec::decodeComplete(const QByteArray &data, QString &transferId)
{
    return decodeEnd(data,transferId);
}
