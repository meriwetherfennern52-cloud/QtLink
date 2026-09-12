#ifndef PACKETPARSER_H
#define PACKETPARSER_H

#include "packet.h"
namespace Protocol{

/**
 * @brief 数据包解析结果。
 */
enum class ParseResult{
    Success, //< 成功解析出一个完整 Packet
    NeedMoreData, //当前数据不足，需要继续接收
    InvalidData  //数据不符合QLink协议
};

/**
 * @brief QtLink TCP 字节流解析器。
 *
 * 负责从持续接收的字节缓冲区中提取一个完整 Packet，
 * 并在成功解析后删除已经消费的字节。
 */
class PacketParser
{
public:
    /**
     * @brief 尝试从接收缓冲区解析一个完整 Packet。
     *
     * @param buffer TCP 接收缓冲区。解析成功后会移除已消费的数据。
     * @param packet 用于保存成功解析得到的数据包。
     * @return 当前解析结果。
     */
    static ParseResult tryParse(QByteArray &buffer,Packet &packet);
};
}

#endif // PACKETPARSER_H
