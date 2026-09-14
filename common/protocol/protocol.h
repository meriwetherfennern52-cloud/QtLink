#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <QtGlobal>
namespace Protocol
{
/**
 * @brief QtLink 协议魔数，用于识别合法的 QtLink 数据包。
 */
constexpr char MAGIC[4]={'Q','L','N','K'};

constexpr qsizetype MAGIC_SIZE=4;

/**
 * @brief 当前 QtLink 协议版本。
 */
constexpr quint8 VERSION = 1;

/**
 * @brief QtLink 固定协议头长度，单位为字节。
 */
constexpr qsizetype HEADER_SIZE=11;

/**
 * @brief 单个 QtLink Packet 允许的最大消息体大小。
 *
 * 用于防止异常或恶意长度字段导致接收缓冲区无限增长。
 */
constexpr quint32 MAX_BODY_SIZE=8*1024*1024;

/**
 * @brief QtLink 消息类型。
 *
 * 底层类型固定为 quint16，因此在协议中占 2 字节。
 */
enum class MessageType:quint16{
    TextSendRequest=1,  //Client->Server
    TextMessage=2       //Server->Client
};

/**
 * @brief 判断网络数据中的消息类型值是否为当前协议支持的类型。
 *
 * @param value 从数据包头读取的原始消息类型值。
 * @return 当前协议支持该类型时返回 true，否则返回 false。
 */
constexpr bool isValidMessageType(quint16 value){
    switch (static_cast<MessageType>(value)) {
    case MessageType::TextSendRequest:
    case MessageType::TextMessage:
        return true;
    default:
        return false;
    }
}

} // namespace Protocol

#endif // PROTOCOL_H
