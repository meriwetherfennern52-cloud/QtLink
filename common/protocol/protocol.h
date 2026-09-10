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
    TextMessage=1
};

} // namespace Protocol

#endif // PROTOCOL_H
