#include "connectionmanager.h"

#include <QTcpSocket>
#include"protocol.h"
#include<QDataStream>
#include"packetcodec.h"
ConnectionManager::ConnectionManager(QObject *parent)
    : QObject{parent},m_socket(new QTcpSocket(this))
{
    connect(m_socket,&QTcpSocket::connected,this,&ConnectionManager::connected);
    connect(m_socket,&QTcpSocket::disconnected,this,&ConnectionManager::disconnected);
    connect(m_socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error){
        emit connectionError(m_socket->errorString());
    });
    connect(m_socket,&QTcpSocket::readyRead,this,&ConnectionManager::handleReadyRead);
}

void ConnectionManager::connectToServer(const QString &host, quint16 port)
{
    m_socket->connectToHost(QHostAddress(host),port);
}

void ConnectionManager::disconnectFromServer()
{
    m_socket->disconnectFromHost();
}

void ConnectionManager::handleReadyRead()
{
    QByteArray newData=m_socket->readAll();
    m_receiveBuffer.append(newData);
    qDebug() << "new data size:" << newData.size()
             << "receive buffer size:" << m_receiveBuffer.size();
    while(true){
        if(m_receiveBuffer.size()<Protocol::HEADER_SIZE){
            break;
        }
        QDataStream stream(m_receiveBuffer);
        stream.setByteOrder(QDataStream::BigEndian);

        char magic[Protocol::MAGIC_SIZE];
        stream.readRawData(magic,Protocol::MAGIC_SIZE);
        if(QByteArray(magic,Protocol::MAGIC_SIZE)!=QByteArray(Protocol::MAGIC,Protocol::MAGIC_SIZE)){
            emit connectionError("Invalid QtLink packet magic");
            m_receiveBuffer.clear();
            disconnectFromServer();
        }

        quint8 version;
        quint16 typeValue;
        quint32 bodyLength;

        stream >> version;
        if (version != Protocol::VERSION)
        {
            emit connectionError("Unsupported QtLink protocol version.");
            m_receiveBuffer.clear();
            disconnectFromServer();
            break;
        }

        stream >> typeValue;
        if (typeValue !=
            static_cast<quint16>(Protocol::MessageType::TextMessage))
        {
            emit connectionError("Unknown QtLink message type.");
            m_receiveBuffer.clear();
            disconnectFromServer();
            break;
        }

        stream >> bodyLength;
        if (bodyLength > Protocol::MAX_BODY_SIZE)
        {
            emit connectionError("QtLink packet body is too large.");
            m_receiveBuffer.clear();
            disconnectFromServer();
            break;
        }

        qsizetype packetSize=Protocol::HEADER_SIZE+static_cast<qsizetype>(bodyLength);
        if(m_receiveBuffer<packetSize){
            break;
        }
        QByteArray packetData=m_receiveBuffer.left(packetSize);
        Protocol::Packet packet;
        if(!Protocol::PacketCodec::decode(packetData,packet)){
            m_receiveBuffer.clear();
            break;
        }
        m_receiveBuffer.remove(0,packetSize);
        qDebug() << "Packet received:"
                 << "type =" << static_cast<quint16>(packet.type)
                 << "body =" << packet.body;
    }
}
