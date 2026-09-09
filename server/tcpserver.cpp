#include "tcpserver.h"

#include <QTcpServer>
#include <QTcpSocket>

TcpServer::TcpServer(QObject *parent)
    : QObject{parent},m_server(new QTcpServer(this))
{
    connect(m_server,&QTcpServer::newConnection,this,&TcpServer::handleNewConnection);
}

bool TcpServer::startServer(quint16 port)
{
    if(m_server->isListening()){
        return true;
    }
    if(!m_server->listen(QHostAddress::Any,port)){
        emit serverError(m_server->errorString());
        return false;
    }
    emit logMessage(QString("Server started on port %1").arg(port));
    return true;
}

void TcpServer::stopServer()
{
    if(!m_server->isListening()){
        return;
    }
    m_server->close();
    for(QTcpSocket*socket:m_clients){
        socket->disconnectFromHost();
    }
    emit logMessage("Server stopped");
}

void TcpServer::handleNewConnection()
{
    while(m_server->hasPendingConnections()){
        QTcpSocket*socket=m_server->nextPendingConnection();
        if(!socket){
            continue;
        }
        m_clients.append(socket);
        emit clientCountChanged(m_clients.size());
        emit logMessage(QString("Client connected:%1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
        connect(socket,&QTcpSocket::disconnected,this,[this,socket](){
            m_clients.removeOne(socket);
            emit clientCountChanged(m_clients.size());
            emit logMessage("Client disconnected");
            socket->deleteLater();
        });
    }
}
