#include "databasemanager.h"
#include<QSqlError>
#include<QSqlQuery>
#include<QDateTime>

DatabaseManager::DatabaseManager(const QString &databasePath)
    :m_databasePath(databasePath)
{
}

bool DatabaseManager::initialize(QString &errorMessage)
{
    m_database=QSqlDatabase::addDatabase("QSQLITE");
    m_database.setDatabaseName(m_databasePath);
    if(!m_database.open()){
        errorMessage=m_database.lastError().text();
        return false;
    }
    if(!createTables(errorMessage)){
        m_database.close();
        return false;
    }
    return true;
}

bool DatabaseManager::saveMessage(MessageDirection direction, const QString &content, QString &errorMessage)
{
    if(!m_database.isOpen()){
        errorMessage=QStringLiteral("Database is not open.");
        return false;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO messages"
        "(direction,content,created_at)"
        "VALUES(:direction,:content,:created_at)"
    ));
    query.bindValue(QStringLiteral(":direction"),static_cast<int>(direction));
    query.bindValue(QStringLiteral(":content"),content);
    query.bindValue(QStringLiteral(":created_at"),QDateTime::currentSecsSinceEpoch());;
    if(!query.exec()){
        errorMessage=query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::loadMessages(QList<MessageRecord> &messages, QString &errorMessage)
{
    if(!m_database.isOpen()){
        errorMessage=QStringLiteral("Database is not open.");
        return false;
    }
    QSqlQuery query(m_database);
    if(!query.exec(QStringLiteral(
            "SELECT direction,content,created_at "
            "FROM messages "
            "ORDER BY id ASC"
            ))){
        errorMessage=query.lastError().text();
        return false;
    }
    messages.clear();

    while(query.next()){
        const int directionValue=query.value(0).toInt();
        if(directionValue!=static_cast<int>(MessageDirection::Sent)&&
            directionValue!=static_cast<int>(MessageDirection::Received)){
            errorMessage=QStringLiteral("Invalid message direction in database.");
            return false;
        }
        MessageRecord record;
        record.direction=static_cast<MessageDirection>(directionValue);
        record.content=query.value(1).toString();
        record.createdAt=query.value(2).toLongLong();
        messages.append(record);
    }
    return true;
}

bool DatabaseManager::createTables(QString &errorMessage)
{
    QSqlQuery query(m_database);
    const QString sql=QStringLiteral(
        "CREATE TABLE IF NOT EXISTS messages("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "direction INTEGER NOT NULL,"
        // "sender_id INTEGER,"
        "content TEXT NOT NULL,"
        "created_at INTEGER NOT NULL"
        ");"
    );
    if(!query.exec(sql)){
        errorMessage=query.lastError().text();
        return false;
    }
    return true;
}
