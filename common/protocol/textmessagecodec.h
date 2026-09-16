#ifndef TEXTMESSAGECODEC_H
#define TEXTMESSAGECODEC_H
#include<QByteArray>
namespace Protocol{

/**
 * @brief QtLink 文本消息 JSON 编解码器。
 *
 * 负责文本消息业务数据与 JSON 字节之间的转换。
 */
class  TextMessageCodec
{
    /**
    * @brief 工具类不允许实例化。
     */
    TextMessageCodec() = delete;

public:
    /**
    * @brief 编码客户端文本发送请求。
    *
    * @param receiverId 目标客户端当前连接 ID。
    * @param content 文本消息内容。
    * @return 编码后的 JSON 字节数据。
    */
    static QByteArray encodeSendRequest(quint32 receiverId,const QString &content);

    /**
    * @brief 解码客户端文本发送请求。
    *
    * @param data JSON 字节数据。
    * @param receiverId 用于接收目标客户端当前连接 ID。
    * @param content 用于接收文本消息内容。
    * @return 解码成功返回 true，否则返回 false。
    */
    static bool decodeSendRequest(const QByteArray &data,quint32 &receiverId,QString &content);

    /**
    * @brief 将服务器下发的文本消息序列化为 JSON 字节。
    *
    * @param senderId 消息发送者的连接 ID。
    * @param content 文本消息内容。
    * @return JSON 格式的文本消息。
    */
    static QByteArray encodeTextMessage(quint32 senderId,const QString &content);

    /**
    * @brief 解析服务器下发的文本消息。
    *
    * @param data JSON 格式的消息数据。
    * @param senderId 用于保存消息发送者的连接 ID。
    * @param content 用于保存文本内容。
    * @return 消息格式合法时返回 true，否则返回 false。
    */
    static bool decodeTextMessage(const QByteArray &data,quint32 &senderId,QString &content);
};

}
#endif // TEXTMESSAGECODEC_H
