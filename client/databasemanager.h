#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H
#include<QString>
#include <QsqlDatabase>
#include<QList>

enum class MessageDirection:int{
    Sent=0,
    Received=1
};

struct MessageRecord{
    MessageDirection direction{MessageDirection::Sent};
    QString content;
    qint64 createdAt{0};
};

class DatabaseManager
{
public:
    /**
     * @brief 创建数据库管理器。
     *
     * @param databasePath SQLite 数据库文件路径。
     */
    explicit DatabaseManager(const QString &databasePath);

    /**
     * @brief 打开数据库并创建项目所需的数据表。
     *
     * @param errorMessage 初始化失败时返回错误信息。
     * @return 初始化成功返回 true，否则返回 false。
     */
    bool initialize(QString &errorMessage);

    /**
    * @brief 将一条聊天消息保存到本地数据库。
    *
    * @param direction 消息方向，发送或接收。
    * @param content 消息正文。
    * @param errorMessage 保存失败时返回错误信息。
    * @return 保存成功返回 true，否则返回 false。
    */
    bool saveMessage(MessageDirection direction,const QString &content,QString &errorMessage);

    /**
     * @brief 加载本地保存的聊天记录。
     *
     * @param messages 用于接收查询到的消息记录。
     * @param errorMessage 查询失败时返回错误信息。
     * @return 加载成功返回 true，否则返回 false。
     */
    bool loadMessage(QList<MessageRecord>&messages,QString &errorMessage);

private:
    /**
     * @brief 创建聊天记录相关数据表。
     *
     * @param errorMessage 创建失败时返回错误信息。
     * @return 创建成功返回 true，否则返回 false。
     */
    bool createTables(QString &errorMessage);

    QString m_databasePath;
    QSqlDatabase m_database;
};

#endif // DATABASEMANAGER_H
