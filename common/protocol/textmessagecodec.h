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
     * @brief 将文本内容序列化为 JSON 字节。
     *
     * @param content 文本消息内容。
     * @return JSON 格式的字节数据。
     */
    static QByteArray encode(const QString &content);

    /**
     * @brief 从 JSON 字节中解析文本消息内容。
     *
     * @param data JSON 格式的字节数据。
     * @param content 用于保存解析得到的文本内容。
     * @return JSON 合法且 content 字段有效时返回 true。
     */
    static bool decode(const QByteArray &data,QString &content);
};

}
#endif // TEXTMESSAGECODEC_H
