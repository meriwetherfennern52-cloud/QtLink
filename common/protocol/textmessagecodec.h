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
    * @brief 将文本发送请求序列化为 JSON 字节。
    *
    * @param content 要发送的文本内容。
    * @return JSON 格式的文本发送请求。
    */
    static QByteArray encodeSendRequest(const QString &content);

    /**
    * @brief 解析客户端发送的文本请求。
    *
    * @param data JSON 格式的请求数据。
    * @param content 用于保存解析得到的文本内容。
    * @return 请求格式合法时返回 true，否则返回 false。
    */
    static bool decodeSendRequest(const QByteArray &data,QString &content);

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
