#include "textmessagecodec.h"
#include<QJsonDocument>
#include<QJsonObject>
#include <limits>

QByteArray Protocol::TextMessageCodec::encodeSendRequest(quint32 receiverId,const QString &content)
{
    QJsonObject jsonObject;
    jsonObject["content"]=content;
    jsonObject["receiverId"]=static_cast<qint64>(receiverId);

    QJsonDocument jsonDocument(jsonObject);
    return jsonDocument.toJson(QJsonDocument::Compact);
}

bool Protocol::TextMessageCodec::decodeSendRequest(const QByteArray &data,quint32 &receiverId, QString &content)
{
    QJsonParseError jsonError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&jsonError);
    if(jsonError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }
    QJsonObject jsonObject=jsonDocument.object();
    const QJsonValue receiverIdValue=jsonObject.value("receiverId");
    QJsonValue contentValue=jsonObject.value("content");
    if(!contentValue.isString()||!receiverIdValue.isDouble()){
        return false;
    }
    const qint64 parsedReceiverId=receiverIdValue.toInteger(-1);
    if (parsedReceiverId <= 0
        || parsedReceiverId >
               static_cast<qint64>(
                   std::numeric_limits<quint32>::max()))
    {
        return false;
    }
    receiverId=static_cast<quint32>(parsedReceiverId);
    content=contentValue.toString();
    return true;
}

QByteArray Protocol::TextMessageCodec::encodeTextMessage(quint32 senderId, const QString &content)
{
    QJsonObject jsonObject;
    jsonObject["content"]=content;
    jsonObject["senderId"]=static_cast<qint64>(senderId);
    QJsonDocument jsonDocument(jsonObject);
    return jsonDocument.toJson(QJsonDocument::Compact);
}

bool Protocol::TextMessageCodec::decodeTextMessage(const QByteArray &data, quint32 &senderId, QString &content)
{
    QJsonParseError jsonError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&jsonError);
    if(jsonError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }
    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue contentValue=jsonObject.value("content");
    QJsonValue senderIdValue=jsonObject.value("senderId");
    if(!contentValue.isString()||!senderIdValue.isDouble()){
        return false;
    }
    content=contentValue.toString();
    qint64 id=senderIdValue.toInteger(-1);
    if(id<=0||id>std::numeric_limits<quint32>::max()){
        return false;
    }
     senderId = static_cast<quint32>(id);
    return true;
}
