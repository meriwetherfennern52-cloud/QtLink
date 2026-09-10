#include "packetcodec.h"
#include<QDataStream> //二进制数据读写工具
#include <QIODevice>
namespace Protocol
{

QByteArray PacketCodec::encode(const Packet &packet)
{
    QByteArray data;
    QDataStream stream(&data,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream.writeRawData(MAGIC,MAGIC_SIZE);
    stream<<VERSION;
    stream<<static_cast<quint16>(packet.type);
    stream<<static_cast<quint32>(packet.body.size());

    stream.writeRawData(packet.body.constData(),packet.body.size());
    return data;
}

bool PacketCodec::decode(const QByteArray &data, Packet &packet)
{
    if(data.size()<HEADER_SIZE){
        return false;
    }
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    char magic[MAGIC_SIZE];
    if(stream.readRawData(magic,MAGIC_SIZE)!=MAGIC_SIZE){
        return false;
    }
    if(QByteArray(magic,MAGIC_SIZE)!=QByteArray(MAGIC,MAGIC_SIZE)){
        return false;
    }
    quint8 version;
    quint16 typeValue;
    quint32 bodyLength;
    stream>>version;
    if(version!=VERSION){
        return false;
    }
    stream>>typeValue;
    if(typeValue!=static_cast<quint16>(MessageType::TextMessage)){
        return false;
    }
    stream>>bodyLength;
    if(data.size()!=HEADER_SIZE+static_cast<qsizetype>(bodyLength)){
        return false;
    }
    packet.type=static_cast<MessageType>(typeValue);
    packet.body=data.mid(HEADER_SIZE,bodyLength);
    return true;
}

}