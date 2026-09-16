#include "onlineclientlistcodec.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <limits>

QByteArray Protocol::OnlineClientListCodec::encode(quint32 selfId,const QList<quint32> &clientIds)
{
    QJsonObject jsonObject;
    jsonObject["selfId"] =static_cast<qint64>(selfId);
    QJsonArray clientIdArray;
    for (quint32 clientId : clientIds)
    {
        clientIdArray.append(static_cast<qint64>(clientId));
    }

    jsonObject["clientIds"] =clientIdArray;

    return QJsonDocument(jsonObject)
        .toJson(QJsonDocument::Compact);
}

bool Protocol::OnlineClientListCodec::decode(const QByteArray &data, quint32 &selfId, QList<quint32> &clientIds)
{
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(data,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()){
        return false;
    }

    const QJsonObject object=document.object();
    const QJsonValue selfIdValue=object.value(QStringLiteral("selfId"));
    const QJsonValue clientIdsValue=object.value(QStringLiteral("clientIds"));

    if(!selfIdValue.isDouble()||!clientIdsValue.isArray()){
        return false;
    }
    const qint64 parsedSelfId=selfIdValue.toInteger(-1);
    if(parsedSelfId<=0
        ||parsedSelfId>static_cast<qint64>(std::numeric_limits<quint32>::max())){
        return false;
    }

    QList<quint32>parsedClientIds;
    const QJsonArray array=clientIdsValue.toArray();
    bool containsSelf=false;
    for(const QJsonValue &value:array){
        if(!value.isDouble()){
            return false;
        }
        const qint64 id=value.toInteger(-1);
        if(id<=0||id>static_cast<qint64>(std::numeric_limits<quint32>::max())){
            return false;
        }
        const quint32 clientId=static_cast<quint32>(id);
        parsedClientIds.append(clientId);
        if (clientId ==
            static_cast<quint32>(parsedSelfId))
        {
            containsSelf = true;
        }
    }
    if(!containsSelf){
        return false;
    }
    selfId=static_cast<quint32>(parsedSelfId);
    clientIds=parsedClientIds;
    return true;
}
