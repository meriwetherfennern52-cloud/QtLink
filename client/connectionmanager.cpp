#include "connectionmanager.h"

#include <QTcpSocket>

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject{parent},m_socket(new QTcpSocket(this))
{
    connect(m_socket,&QTcpSocket::connected,this,&ConnectionManager::connected);
    connect(m_socket,&QTcpSocket::disconnected,this,&ConnectionManager::disconnected);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error){
        emit connectionError(m_socket->errorString());
    });
}

void ConnectionManager::connectToServer(const QString &host, quint16 port)
{
    m_socket->connectToHost(QHostAddress(host),port);
}

void ConnectionManager::disconnectFromServer()
{
    m_socket->disconnectFromHost();
}
