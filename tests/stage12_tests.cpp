#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSignalSpy>

#define private public
#include "tcpserver.h"
#include "connectionmanager.h"
#include "filetransfermanager.h"
#undef private

#include "filechunkcodec.h"
#include "filetransfercontrolcodec.h"
#include "packetcodec.h"
#include "packetparser.h"

namespace {

const QString transferIdA=
    QStringLiteral("550e8400-e29b-41d4-a716-446655440000");
const QString transferIdB=
    QStringLiteral("550e8400-e29b-41d4-a716-446655440001");

class SocketHarness
{
public:
    SocketHarness()
    {
        listener.listen(QHostAddress::LocalHost,0);
    }

    ~SocketHarness()
    {
        qDeleteAll(clients);
    }

    QTcpSocket *addServerSocket()
    {
        QTcpSocket *client=new QTcpSocket;
        client->connectToHost(QHostAddress::LocalHost,listener.serverPort());
        if(!client->waitForConnected(1000)
            ||!listener.waitForNewConnection(1000)){

            delete client;
            return nullptr;
        }

        clients.append(client);
        return listener.nextPendingConnection();
    }

    QTcpSocket *clientAt(qsizetype index) const
    {
        return clients.at(index);
    }

private:
    QTcpServer listener;
    QList<QTcpSocket*>clients;
};

bool connectManager(ConnectionManager &manager,QTcpServer &listener,
                    QTcpSocket *&peer)
{
    if(!listener.listen(QHostAddress::LocalHost,0)){
        return false;
    }

    manager.connectToServer(
        QHostAddress(QHostAddress::LocalHost).toString(),listener.serverPort());
    if(!manager.m_socket->waitForConnected(1000)
        ||!listener.waitForNewConnection(1000)){

        return false;
    }

    peer=listener.nextPendingConnection();
    return peer!=nullptr;
}

void addSession(TcpServer &server,quint32 clientId,QTcpSocket *socket)
{
    TcpServer::ClientSession session;
    session.socket=socket;
    server.m_sessions.insert(clientId,session);
}

void addTransfer(TcpServer &server,const QString &transferId,
                 quint32 senderId,quint32 receiverId,qint64 fileSize,
                 qint64 nextOffset,TcpServer::TransferPhase phase)
{
    TcpServer::ActiveTransfer transfer;
    transfer.senderId=senderId;
    transfer.receiverId=receiverId;
    transfer.fileSize=fileSize;
    transfer.nextOffset=nextOffset;
    transfer.phase=phase;
    transfer.lastActivityMs=server.m_activityClock.elapsed();
    server.m_activeTransfers.insert(transferId,transfer);
}

QByteArray endBody(const QString &transferId)
{
    return Protocol::FileTransferControlCodec::encodeEnd(transferId);
}

QByteArray abortBody(const QString &transferId)
{
    return Protocol::FileTransferControlCodec::encodeAbort(
        transferId,Protocol::FileAbortReason::Cancelled);
}

}

class Stage12Tests:public QObject
{
    Q_OBJECT

private slots:
    void serverCompleteRequiresReceiverAndAwaitingPhase();
    void serverRejectsChunkAndDuplicateEndAfterEnd();
    void serverAbortWorksInBothPhases();
    void serverFirstTerminalEventWins();
    void serverZeroByteTransferCompletes();
    void serverDisconnectWhileAwaitingCompleteCleansTransfer();
    void serverTimeoutCleansTransferAndResumesSender();
    void serverReceiverActivityOnlyRefreshesTransferringTransfer();
    void serverMalformedActiveRequestDisconnectsAndCleans();
    void serverUnrelatedClientCannotAbortTransfer();
    void senderFinishingWaitsForMatchingComplete();
    void senderAbortAndDisconnectResetState();
    void receiverEndSendsCompleteRequest();
    void receiverCompleteSendFailureKeepsFile();
    void outgoingPumpFailureReturnsFalse();
    void clientInvalidTerminalMessageDisconnects();
};

void Stage12Tests::serverCompleteRequiresReceiverAndAwaitingPhase()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QTcpSocket *unrelatedSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    QVERIFY(unrelatedSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);
    addSession(server,3,unrelatedSocket);

    addTransfer(server,transferIdA,1,2,0,0,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    server.handleFileCompleteRequest(1,endBody(transferIdA));
    QVERIFY(server.m_activeTransfers.contains(transferIdA));

    server.handleFileCompleteRequest(3,endBody(transferIdA));
    QVERIFY(server.m_activeTransfers.contains(transferIdA));

    server.m_activeTransfers[transferIdA].phase=
        TcpServer::TransferPhase::Transferring;
    server.handleFileCompleteRequest(2,endBody(transferIdA));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
}

void Stage12Tests::serverRejectsChunkAndDuplicateEndAfterEnd()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);

    addTransfer(server,transferIdA,1,2,1,1,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    const QByteArray chunkBody=Protocol::FileChunkCodec::encode(
        transferIdA,1,QByteArray("x"));
    server.handleFileChunkRequest(1,chunkBody);
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));

    addTransfer(server,transferIdB,1,2,0,0,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    server.handleFileEndRequest(1,endBody(transferIdB));
    QVERIFY(!server.m_activeTransfers.contains(transferIdB));
}

void Stage12Tests::serverAbortWorksInBothPhases()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);

    addTransfer(server,transferIdA,1,2,1,0,
                TcpServer::TransferPhase::Transferring);
    server.handleFileAbortRequest(1,abortBody(transferIdA));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));

    addTransfer(server,transferIdB,1,2,1,1,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    server.handleFileAbortRequest(2,abortBody(transferIdB));
    QVERIFY(!server.m_activeTransfers.contains(transferIdB));
}

void Stage12Tests::serverFirstTerminalEventWins()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);

    addTransfer(server,transferIdA,1,2,0,0,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    server.handleFileCompleteRequest(2,endBody(transferIdA));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
    server.handleFileAbortRequest(1,abortBody(transferIdA));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));

    addTransfer(server,transferIdB,1,2,0,0,
                TcpServer::TransferPhase::AwaitingReceiverComplete);
    server.handleFileAbortRequest(2,abortBody(transferIdB));
    QVERIFY(!server.m_activeTransfers.contains(transferIdB));
    server.handleFileCompleteRequest(2,endBody(transferIdB));
    QVERIFY(!server.m_activeTransfers.contains(transferIdB));
}

void Stage12Tests::serverZeroByteTransferCompletes()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);

    const QByteArray startBody=
        Protocol::FileTransferControlCodec::encodeStartRequest(
            2,transferIdA,QStringLiteral("empty.txt"),0);
    server.handleFileStartRequest(1,startBody);
    QVERIFY(server.m_activeTransfers.contains(transferIdA));
    QCOMPARE(server.m_activeTransfers.value(transferIdA).phase,
             TcpServer::TransferPhase::Transferring);

    server.handleFileEndRequest(1,endBody(transferIdA));
    QVERIFY(server.m_activeTransfers.contains(transferIdA));
    QCOMPARE(server.m_activeTransfers.value(transferIdA).phase,
             TcpServer::TransferPhase::AwaitingReceiverComplete);

    server.handleFileCompleteRequest(2,endBody(transferIdA));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
}

void Stage12Tests::serverDisconnectWhileAwaitingCompleteCleansTransfer()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);
    addTransfer(server,transferIdA,1,2,0,0,
                TcpServer::TransferPhase::AwaitingReceiverComplete);

    server.cleanupTransfersForClient(2);
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
}

void Stage12Tests::serverTimeoutCleansTransferAndResumesSender()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);
    addTransfer(server,transferIdA,1,2,1024,0,
                TcpServer::TransferPhase::Transferring);
    server.m_activeTransfers[transferIdA].lastActivityMs=
        server.m_activityClock.elapsed()
        -TcpServer::TRANSFER_STALL_TIMEOUT_MS-1;
    server.m_sessions[1].readPaused=true;
    server.m_sessions[1].blockedReceiverId=2;

    server.checkStalledTransfers();

    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
    QVERIFY(!server.m_sessions[1].readPaused);
    QCOMPARE(server.m_sessions[1].blockedReceiverId,quint32(0));
}

void Stage12Tests::serverReceiverActivityOnlyRefreshesTransferringTransfer()
{
    TcpServer server;
    addTransfer(server,transferIdA,1,2,1024,0,
                TcpServer::TransferPhase::Transferring);
    addTransfer(server,transferIdB,3,2,1024,1024,
                TcpServer::TransferPhase::AwaitingReceiverComplete);

    const qint64 oldTime=server.m_activityClock.elapsed()-1000;
    server.m_activeTransfers[transferIdA].lastActivityMs=oldTime;
    server.m_activeTransfers[transferIdB].lastActivityMs=oldTime;

    server.updateReceiverTransferActivity(2);

    QVERIFY(server.m_activeTransfers.value(transferIdA).lastActivityMs
            >oldTime);
    QCOMPARE(server.m_activeTransfers.value(transferIdB).lastActivityMs,
             oldTime);
}

void Stage12Tests::serverMalformedActiveRequestDisconnectsAndCleans()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);
    addTransfer(server,transferIdA,1,2,1024,0,
                TcpServer::TransferPhase::Transferring);

    connect(senderSocket,&QTcpSocket::disconnected,&server,[&server](){
        server.cleanupTransfersForClient(1);
        server.m_sessions.remove(1);
    });

    server.handleFileChunkRequest(1,QByteArray("invalid"));
    QTRY_VERIFY(!server.m_sessions.contains(1));
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));
}

void Stage12Tests::serverUnrelatedClientCannotAbortTransfer()
{
    SocketHarness sockets;
    TcpServer server;
    QTcpSocket *senderSocket=sockets.addServerSocket();
    QTcpSocket *receiverSocket=sockets.addServerSocket();
    QTcpSocket *unrelatedSocket=sockets.addServerSocket();
    QVERIFY(senderSocket);
    QVERIFY(receiverSocket);
    QVERIFY(unrelatedSocket);
    addSession(server,1,senderSocket);
    addSession(server,2,receiverSocket);
    addSession(server,3,unrelatedSocket);
    addTransfer(server,transferIdA,1,2,1024,0,
                TcpServer::TransferPhase::Transferring);

    server.handleFileAbortRequest(3,abortBody(transferIdA));
    QVERIFY(server.m_activeTransfers.contains(transferIdA));
}

void Stage12Tests::senderFinishingWaitsForMatchingComplete()
{
    ConnectionManager connection;
    FileTransferManager manager(&connection);
    manager.m_sendState=FileTransferManager::SendState::Finishing;
    manager.m_sendTransferId=transferIdA;
    manager.m_sendFileSize=1;
    manager.m_sendOffset=1;

    manager.handleSocketBytesWritten(1);
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Finishing);
    QCOMPARE(manager.m_sendTransferId,transferIdA);

    manager.handleFileCompleteReceived(transferIdB);
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Finishing);

    manager.handleFileCompleteReceived(transferIdA);
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(manager.m_sendTransferId.isEmpty());
}

void Stage12Tests::senderAbortAndDisconnectResetState()
{
    ConnectionManager connection;
    FileTransferManager manager(&connection);
    manager.m_sendState=FileTransferManager::SendState::Finishing;
    manager.m_sendTransferId=transferIdA;
    manager.handleFileAbortReceived(
        transferIdA,Protocol::FileAbortReason::Timeout);
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Idle);

    manager.m_sendState=FileTransferManager::SendState::Finishing;
    manager.m_sendTransferId=transferIdB;
    manager.handleConnectionDisconnected();
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(manager.m_sendTransferId.isEmpty());
}

void Stage12Tests::receiverEndSendsCompleteRequest()
{
    QTcpServer listener;
    ConnectionManager connection;
    QTcpSocket *peer=nullptr;
    QVERIFY(connectManager(connection,listener,peer));
    FileTransferManager manager(&connection);
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString filePath=temporaryDir.filePath("received.bin");
    QFile *file=new QFile(filePath,&manager);
    QVERIFY(file->open(QIODevice::WriteOnly|QIODevice::NewOnly));
    QCOMPARE(file->write("abc",3),qint64(3));

    FileTransferManager::IncomingTransfer transfer;
    transfer.senderId=1;
    transfer.fileSize=3;
    transfer.nextOffset=3;
    transfer.file=file;
    manager.m_incomingTransfers.insert(transferIdA,transfer);
    QSignalSpy completedSpy(&manager,&FileTransferManager::fileReceiveCompleted);

    manager.handleFileEndReceived(transferIdA);

    QCOMPARE(completedSpy.count(),1);
    QVERIFY(!manager.m_incomingTransfers.contains(transferIdA));
    QVERIFY(QFileInfo::exists(filePath));
    QTRY_VERIFY(peer->bytesAvailable()>0);
    QByteArray buffer=peer->readAll();
    Protocol::Packet packet;
    QCOMPARE(Protocol::PacketParser::tryParse(buffer,packet),
             Protocol::ParseResult::Success);
    QCOMPARE(packet.type,Protocol::MessageType::FileCompleteRequest);
    QString completedTransferId;
    QVERIFY(Protocol::FileTransferControlCodec::decodeComplete(
        packet.body,completedTransferId));
    QCOMPARE(completedTransferId,transferIdA);
}

void Stage12Tests::receiverCompleteSendFailureKeepsFile()
{
    ConnectionManager connection;
    FileTransferManager manager(&connection);
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString filePath=temporaryDir.filePath("received.bin");
    QFile *file=new QFile(filePath,&manager);
    QVERIFY(file->open(QIODevice::WriteOnly|QIODevice::NewOnly));
    QCOMPARE(file->write("abc",3),qint64(3));

    FileTransferManager::IncomingTransfer transfer;
    transfer.senderId=1;
    transfer.fileSize=3;
    transfer.nextOffset=3;
    transfer.file=file;
    manager.m_incomingTransfers.insert(transferIdA,transfer);

    manager.handleFileEndReceived(transferIdA);

    QVERIFY(!manager.m_incomingTransfers.contains(transferIdA));
    QVERIFY(QFileInfo::exists(filePath));
    QFile savedFile(filePath);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    QCOMPARE(savedFile.readAll(),QByteArray("abc"));
}

void Stage12Tests::outgoingPumpFailureReturnsFalse()
{
    ConnectionManager connection;
    FileTransferManager manager(&connection);
    manager.m_sendState=FileTransferManager::SendState::Sending;
    manager.m_sendTransferId=transferIdA;
    manager.m_sendFileSize=1;
    manager.m_sendOffset=0;

    QVERIFY(!manager.pumpSendQueue());
    QCOMPARE(manager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(manager.m_sendTransferId.isEmpty());
}

void Stage12Tests::clientInvalidTerminalMessageDisconnects()
{
    QTcpServer listener;
    ConnectionManager connection;
    QTcpSocket *peer=nullptr;
    QVERIFY(connectManager(connection,listener,peer));
    QSignalSpy disconnectedSpy(&connection,&ConnectionManager::disconnected);

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::FileComplete;
    packet.body=QByteArray("invalid");
    peer->write(Protocol::PacketCodec::encode(packet));
    peer->flush();

    QTRY_COMPARE(disconnectedSpy.count(),1);
}

QTEST_GUILESS_MAIN(Stage12Tests)

#include "stage12_tests.moc"
