#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include <QFile>
#include<QObject>
#include<QHash>

class ConnectionManager;

class FileTransferManager:public QObject
{
    Q_OBJECT
public:
    /**
     * @brief 创建文件传输管理器。
     * @param connectionManager 客户端网络连接管理器。
     * @param parent 父对象。
     */
    explicit FileTransferManager(ConnectionManager*connectionManager,QObject *parent=nullptr);

    /**
     * @brief 准备并开始向指定客户端发送文件。
     * @param filePath 本地文件完整路径。
     * @param receiverId 目标客户端连接 ID。
     * @return 成功启动返回 true，否则返回 false。
     */
    bool sendFile(const QString &filePath,quint32 receiverId);

private:
    /**
     * @brief 使用安全且不重名的路径原子创建接收文件。
     * @param fileName 网络消息中的文件名。
     * @return 已打开的接收文件，失败时返回 nullptr。
     */
    QFile *createReceiveFile(const QString &fileName);

    /**
    * @brief 读取并发送当前文件的下一个数据块。
    * @return 成功发送一个数据块返回 true，否则返回 false。
    */
    bool sendNextChunk();

    /**
    * @brief 根据 Socket 写缓冲区状态持续填充文件数据块。
    *
    * 当待发送字节低于高水位时继续读取并发送 Chunk；
    * 达到高水位或文件数据全部入队后停止。
    */
    void pumpSendQueue();

    /**
    * @brief 将当前发送任务的 FileEndRequest 加入网络发送流程。
    * @return 成功加入发送流程返回 true，否则返回 false。
    */
    bool finishSendTransfer();

    /**
    * @brief 清理当前主动文件发送任务并恢复 Idle 状态。
    */
    void resetSendTransfer();

signals:
    /**
     * @brief 文件已完整接收并保存到本地。
     * @param filePath 文件最终保存路径。
     */
    void fileReceiveCompleted(const QString &filePath);

    /**
     * @brief 当前主动文件发送的数据提交进度发生变化。
     * @param sentBytes 已提交到发送流程的文件数据字节数。
     * @param totalBytes 文件总字节数。
     */
    void sendProgressChanged(qint64 sentBytes,qint64 totalBytes);

private slots:
    /**
    * @brief 处理服务器转发的文件传输开始消息。
    * @param senderId 文件发送客户端 ID。
    * @param transferId 本次文件传输唯一标识。
    * @param fileName 文件名。
    * @param fileSize 文件总字节数。
    */
    void handleFileStartReceived(quint32 senderId,const QString&transferId,
                                 const QString&fileName,qint64 fileSize);

    /**
    * @brief 处理接收到的文件数据块。
    * @param transferId 本次文件传输唯一标识。
    * @param offset 当前数据块在文件中的起始偏移。
    * @param chunkData 文件数据块。
    */
    void handleFileChunkReceived(const QString &transferId,qint64 offset,
        const QByteArray &chunkData);

    /**
    * @brief 处理 Socket 写出数据事件并推进文件发送流程。
    * @param bytes 本次从 Socket 写缓冲区写出的字节数。
    */
    void handleSocketBytesWritten(qint64 bytes);

    /**
    * @brief 处理服务器转发的文件传输结束消息。
    * @param transferId 本次文件传输唯一标识。
    */
    void handleFileEndReceived(const QString &transferId);

    /**
    * @brief 处理服务器连接断开事件，并终止当前所有文件传输。
    */
    void handleConnectionDisconnected();


private:
    struct IncomingTransfer{
        quint32 senderId{0};
        qint64 fileSize{0};
        qint64 nextOffset{0};
        QFile*file{nullptr};
    };
    QHash<QString,IncomingTransfer>m_incomingTransfers;

    enum class SendState{
        Idle,
        Starting,
        Sending,
        Finishing
    };

    ConnectionManager*m_connectionManager;

    SendState m_sendState{SendState::Idle};
    QFile m_sendFile;
    QString m_sendTransferId;
    qint64 m_sendFileSize{0};
    qint64 m_sendOffset{0};

    static constexpr qint64 SEND_HIGH_WATER_MARK=256*1024;
};

#endif // FILETRANSFERMANAGER_H
