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
    TextSendRequest=1,    //Client->Server
    TextMessage=2,        //Server->Client
    OnlineClientList=3,   //Server->Client

    FileStartRequest=4,
    FileStart=5,
    FileChunkRequest=6,
    FileChunk=7,
    FileEndRequest=8,
    FileEnd=9,

    FileAbortRequest=10,
    FileAbort=11,

    FileCompleteRequest = 12,
    FileComplete = 13,

    HeartbeatRequest = 14,
    HeartbeatResponse = 15
};

/**
 * @brief 文件传输异常终止原因。
 */
enum class FileAbortReason:quint8{
    PeerDisconnected=1,
    IoError=2,
    Cancelled=3,
    ProtocolError=4,
    Timeout=5
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
    case MessageType::OnlineClientList:
    case MessageType::FileStartRequest:
    case MessageType::FileStart:
    case MessageType::FileChunkRequest:
    case MessageType::FileChunk:
    case MessageType::FileEndRequest:
    case MessageType::FileEnd:
    case MessageType::FileAbortRequest:
    case MessageType::FileAbort:
    case MessageType::FileCompleteRequest:
    case MessageType::FileComplete:
    case MessageType::HeartbeatRequest:
    case MessageType::HeartbeatResponse:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 判断文件传输终止原因值是否为当前协议支持的类型。
 *
 * @param value 网络消息中的原始原因值。
 * @return 当前协议支持该原因时返回 true，否则返回 false。
 */
constexpr bool isValidFileAbortReason(quint8 value){
    switch (static_cast<FileAbortReason>(value)) {
    case FileAbortReason::PeerDisconnected:
    case FileAbortReason::IoError:
    case FileAbortReason::Cancelled:
    case FileAbortReason::ProtocolError:
    case Protocol::FileAbortReason::Timeout:
        return true;
    default:
        return false;
    }
}

} // namespace Protocol

#endif // PROTOCOL_H
