#include "packetparser.h"
#include"protocol.h"
#include"packet.h"
#include"packetcodec.h"
#include<QDataStream>
Protocol::ParseResult Protocol::PacketParser::tryParse(QByteArray &buffer, Packet &packet)
{
    if(buffer.size()<HEADER_SIZE){
        return ParseResult::NeedMoreData;
    }
    QDataStream stream(buffer);
    stream.setByteOrder(QDataStream::BigEndian);

    char magic[MAGIC_SIZE];
    stream.readRawData(magic,MAGIC_SIZE);
    if(QByteArray(magic,MAGIC_SIZE)!=QByteArray(MAGIC,MAGIC_SIZE)){
        return ParseResult::InvalidData;
    }

    quint8 version;
    quint16 typeValue;
    quint32 bodyLength;

    stream >> version;
    if (version != VERSION)
    {
        return ParseResult::InvalidData;
    }

    stream >> typeValue;
    if (typeValue !=
        static_cast<quint16>(MessageType::TextMessage))
    {
        return ParseResult::InvalidData;
    }

    stream >> bodyLength;
    if (bodyLength > MAX_BODY_SIZE)
    {
        return ParseResult::InvalidData;
    }

    qsizetype packetSize=HEADER_SIZE+static_cast<qsizetype>(bodyLength);
    if(buffer.size()<packetSize){
        return ParseResult::NeedMoreData;
    }
    QByteArray packetData=buffer.left(packetSize);
    if(!PacketCodec::decode(packetData,packet)){
        return ParseResult::InvalidData;
    }
    buffer.remove(0,packetSize);
    return ParseResult::Success;
}
