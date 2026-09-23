#ifndef FILECHUNKCODEC_H
#define FILECHUNKCODEC_H

#include<QByteArray>
#include <QString>
#include <QtGlobal>

namespace Protocol{

static constexpr qint64 MAX_FILE_CHUNK_DATA_SIZE=64*1024;

class FileChunkCodec
{
public:
    FileChunkCodec()=delete;

    /**
     * @brief 编码文件数据块消息体。
     *
     * @param transferId 本次文件传输的唯一标识。
     * @param offset 当前数据块在文件中的起始字节偏移。
     * @param chunkData 当前文件数据块。
     * @return 编码后的二进制消息体。
     */
    static QByteArray encode(const QString &transferId,qint64 offset,
                             const QByteArray &chunkData);

    /**
     * @brief 解码文件数据块消息体。
     *
     * @param data 二进制消息体。
     * @param transferId 用于接收本次文件传输标识。
     * @param offset 用于接收当前数据块的起始字节偏移。
     * @param chunkData 用于接收文件数据块。
     * @return 解码成功返回 true，否则返回 false。
     */
    static bool decode(const QByteArray &data,QString &transferId,
                       qint64 &offset,QByteArray &chunkData);
};

};

#endif // FILECHUNKCODEC_H
