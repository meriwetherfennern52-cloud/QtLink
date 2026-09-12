#ifndef PACKETCODEC_H
#define PACKETCODEC_H
#include "packet.h"
#include<QByteArray>
namespace Protocol{
/**
 * @brief QtLink 数据包编解码器。
 *
 * 负责 Packet 与网络字节序列之间的转换。
 */
class PacketCodec{
public:

    /**
     * @brief 工具类不允许实例化。
     */
    PacketCodec() = delete;

    /**
     * @brief 将 Packet 序列化为可发送的字节序列。
     */
    static QByteArray encode(const Packet &packet);

    /**
     * @brief 将一个完整的数据包反序列化为 Packet。
     *
     * @return 数据合法时返回 true，否则返回 false。
     */
    static bool decode(const QByteArray &data,Packet &packet);
};
}
#endif // PACKETCODEC_H
