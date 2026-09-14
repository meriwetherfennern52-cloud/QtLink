#include "textmessagecodec.h"
#include<QJsonDocument>
#include<QJsonObject>

QByteArray Protocol::TextMessageCodec::encodeSendRequest(const QString &content)
{
    QJsonObject jsonObject;
    jsonObject["content"]=content;
    QJsonDocument jsonDocument(jsonObject);
    return jsonDocument.toJson(QJsonDocument::Compact);
}

bool Protocol::TextMessageCodec::decodeSendRequest(const QByteArray &data, QString &content)
{
    QJsonParseError jsonError;
    QJsonDocument jsonDocument=QJsonDocument::fromJson(data,&jsonError);
    if(jsonError.error!=QJsonParseError::NoError||!jsonDocument.isObject()){
        return false;
    }
    QJsonObject jsonObject=jsonDocument.object();
    QJsonValue contentValue=jsonObject.value("content");
    if(!contentValue.isString()){
        return false;
    }
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
