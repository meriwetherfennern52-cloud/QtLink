#include "textmessagecodec.h"
#include<QJsonDocument>
#include<QJsonObject>

QByteArray Protocol::TextMessageCodec::encode(const QString &content)
{
    QJsonObject jsonObject;
    jsonObject["content"]=content;
    QJsonDocument jsonDocument(jsonObject);
    return jsonDocument.toJson(QJsonDocument::Compact);

}

bool Protocol::TextMessageCodec::decode(const QByteArray &data, QString &content)
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
