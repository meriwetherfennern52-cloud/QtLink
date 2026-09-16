#ifndef ONLINECLIENTLISTCODEC_H
#define ONLINECLIENTLISTCODEC_H
#include <QByteArray>
#include<QList>

namespace Protocol{

/**
 * @brief 在线客户端列表消息的 JSON 编解码工具类。
 */
class OnlineClientListCodec
{

public:
    OnlineClientListCodec()=delete;

    /**
     * @brief 编码在线客户端列表。
     *
     * @param selfId 接收该消息的客户端自身连接 ID。
     * @param clientIds 当前所有在线客户端连接 ID。
     * @return 编码后的 JSON 字节数据。
     */
    static QByteArray encode(quint32 selfId,const QList<quint32>&clientIds);

    /**
     * @brief 解码在线客户端列表。
     *
     * @param data JSON 字节数据。
     * @param selfId 用于接收当前客户端自身连接 ID。
     * @param clientIds 用于接收当前所有在线客户端连接 ID。
     * @return 解码成功返回 true，否则返回 false。
     */
    static bool decode(const QByteArray &data,quint32 &selfId,QList<quint32>&clientIds);
};

};

#endif // ONLINECLIENTLISTCODEC_H
