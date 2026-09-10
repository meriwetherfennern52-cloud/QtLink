#ifndef PACKET_H
#define PACKET_H
#include"protocol.h"
#include<QByteArray>
namespace Protocol {
/**
 * @brief 表示一条已经构造或解析完成的 QtLink 消息。
 */
struct Packet{
    MessageType type;
    QByteArray body;
};
}
#endif // PACKET_H
