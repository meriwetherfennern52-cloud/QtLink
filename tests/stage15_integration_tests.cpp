#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#define private public
#include "tcpserver.h"
#include "connectionmanager.h"
#include "filetransfermanager.h"
#undef private

#include "databasemanager.h"
#include "filechunkcodec.h"
#include "packetcodec.h"

namespace {

const QString transferIdA=
    QStringLiteral("550e8400-e29b-41d4-a716-446655440100");
const QString transferIdB=
    QStringLiteral("550e8400-e29b-41d4-a716-446655440101");

struct ClientIdentity
{
    quint32 selfId{0};
    QList<quint32> clientIds;
};

void trackIdentity(ConnectionManager &manager,ClientIdentity &identity)
{
    QObject::connect(&manager,&ConnectionManager::onlineClientListReceived,
                     &manager,[&identity](quint32 selfId,
                              const QList<quint32> &clientIds){
        identity.selfId=selfId;
        identity.clientIds=clientIds;
    });
}

void disconnectClient(ConnectionManager &manager)
{
    manager.disconnectFromServer();
    QCoreApplication::processEvents();
}

}

class Stage15IntegrationTests:public QObject
{
    Q_OBJECT

private slots:
    void twoClientsConnectAndExchangeText();
    void fileTransferControlFlowCompletesEndToEnd();
    void receiverDisconnectDuringTransferConverges();
    void serverRestartCreatesFreshSessionAndSupportsNewWork();
    void heartbeatAndServerWatchdogInteractCorrectly();
    void malformedTextRequestDisconnectsClient();
    void malformedServerTextStopsReconnect();
    void databaseHistorySurvivesManagerReopen();
    void destructionWithPendingReconnectTimerIsSafe();
};

void Stage15IntegrationTests::twoClientsConnectAndExchangeText()
{
    TcpServer server;
    QVERIFY(server.startServer(0));
    const quint16 port=server.m_server->serverPort();
    QVERIFY(port!=0);

    ConnectionManager sender;
    ConnectionManager receiver;
    ClientIdentity senderIdentity;
    ClientIdentity receiverIdentity;
    trackIdentity(sender,senderIdentity);
    trackIdentity(receiver,receiverIdentity);
    QSignalSpy receivedSpy(&receiver,&ConnectionManager::textMessageReceived);

    sender.connectToServer(QStringLiteral("127.0.0.1"),port);
    receiver.connectToServer(QStringLiteral("127.0.0.1"),port);

    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),2,2000);
    QTRY_VERIFY_WITH_TIMEOUT(senderIdentity.selfId!=0,2000);
    QTRY_VERIFY_WITH_TIMEOUT(receiverIdentity.selfId!=0,2000);
    QVERIFY(senderIdentity.selfId!=receiverIdentity.selfId);

    const QString content=QStringLiteral("Stage 15 集成消息 🚀");
    QVERIFY(sender.sendTextMessage(receiverIdentity.selfId,content));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(),1,2000);
    QCOMPARE(receivedSpy.at(0).at(0).toUInt(),senderIdentity.selfId);
    QCOMPARE(receivedSpy.at(0).at(1).toString(),content);

    disconnectClient(sender);
    disconnectClient(receiver);
    QTRY_VERIFY_WITH_TIMEOUT(server.m_sessions.isEmpty(),2000);
    server.stopServer();
}

void Stage15IntegrationTests::fileTransferControlFlowCompletesEndToEnd()
{
    TcpServer server;
    QVERIFY(server.startServer(0));
    const quint16 port=server.m_server->serverPort();

    ConnectionManager sender;
    ConnectionManager receiver;
    ClientIdentity senderIdentity;
    ClientIdentity receiverIdentity;
    trackIdentity(sender,senderIdentity);
    trackIdentity(receiver,receiverIdentity);

    QSignalSpy startSpy(&receiver,&ConnectionManager::fileStartReceived);
    QSignalSpy chunkSpy(&receiver,&ConnectionManager::fileChunkReceived);
    QSignalSpy endSpy(&receiver,&ConnectionManager::fileEndReceived);
    QSignalSpy completeSpy(&sender,&ConnectionManager::fileCompleteReceived);

    sender.connectToServer(QStringLiteral("127.0.0.1"),port);
    receiver.connectToServer(QStringLiteral("127.0.0.1"),port);
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),2,2000);
    QTRY_VERIFY_WITH_TIMEOUT(receiverIdentity.selfId!=0,2000);

    QByteArray chunkData;
    chunkData.append("abc",3);
    chunkData.append('\0');
    chunkData.append(char(0xff));

    QVERIFY(sender.sendFileStartRequest(
        receiverIdentity.selfId,transferIdA,QStringLiteral("report.bin"),
        chunkData.size()));
    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(),1,2000);
    QVERIFY(sender.sendFileChunkRequest(transferIdA,0,chunkData));
    QTRY_COMPARE_WITH_TIMEOUT(chunkSpy.count(),1,2000);
    QCOMPARE(chunkSpy.at(0).at(2).toByteArray(),chunkData);

    QVERIFY(sender.sendFileEndRequest(transferIdA));
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(),1,2000);
    QVERIFY(server.m_activeTransfers.contains(transferIdA));
    QCOMPARE(server.m_activeTransfers.value(transferIdA).phase,
             TcpServer::TransferPhase::AwaitingReceiverComplete);

    QVERIFY(receiver.sendFileCompleteRequest(transferIdA));
    QTRY_COMPARE_WITH_TIMEOUT(completeSpy.count(),1,2000);
    QVERIFY(!server.m_activeTransfers.contains(transferIdA));

    disconnectClient(sender);
    disconnectClient(receiver);
    server.stopServer();
}

void Stage15IntegrationTests::receiverDisconnectDuringTransferConverges()
{
    TcpServer server;
    QVERIFY(server.startServer(0));
    const quint16 port=server.m_server->serverPort();

    ConnectionManager sender;
    ConnectionManager receiver;
    ClientIdentity senderIdentity;
    ClientIdentity receiverIdentity;
    trackIdentity(sender,senderIdentity);
    trackIdentity(receiver,receiverIdentity);
    bool senderSawAbort=false;
    QObject::connect(&sender,&ConnectionManager::fileAbortReceived,
                     &sender,[&senderSawAbort](const QString &transferId,
                              Protocol::FileAbortReason){
        if(transferId==transferIdA){
            senderSawAbort=true;
        }
    });

    sender.connectToServer(QStringLiteral("127.0.0.1"),port);
    receiver.connectToServer(QStringLiteral("127.0.0.1"),port);
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),2,2000);
    QTRY_VERIFY_WITH_TIMEOUT(receiverIdentity.selfId!=0,2000);

    QVERIFY(sender.sendFileStartRequest(
        receiverIdentity.selfId,transferIdA,QStringLiteral("partial.bin"),8));
    QTRY_VERIFY_WITH_TIMEOUT(server.m_activeTransfers.contains(transferIdA),2000);

    receiver.disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(!server.m_activeTransfers.contains(transferIdA),2000);
    QTRY_VERIFY_WITH_TIMEOUT(senderSawAbort,2000);
    QCOMPARE(sender.m_connectionState,ConnectionManager::ConnectionState::Connected);

    disconnectClient(sender);
    server.stopServer();
}

void Stage15IntegrationTests::serverRestartCreatesFreshSessionAndSupportsNewWork()
{
    TcpServer server;
    QVERIFY(server.startServer(0));
    const quint16 port=server.m_server->serverPort();

    ConnectionManager first;
    ConnectionManager second;
    ClientIdentity firstIdentity;
    ClientIdentity secondIdentity;
    trackIdentity(first,firstIdentity);
    trackIdentity(second,secondIdentity);

    first.connectToServer(QStringLiteral("127.0.0.1"),port);
    second.connectToServer(QStringLiteral("127.0.0.1"),port);
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),2,2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondIdentity.selfId!=0,2000);

    const quint32 oldFirstId=firstIdentity.selfId;
    const quint32 oldSecondId=secondIdentity.selfId;
    QVERIFY(first.sendFileStartRequest(
        oldSecondId,transferIdA,QStringLiteral("old.bin"),4));
    QTRY_VERIFY_WITH_TIMEOUT(server.m_activeTransfers.contains(transferIdA),2000);

    server.stopServer();
    QTRY_VERIFY_WITH_TIMEOUT(server.m_sessions.isEmpty(),2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.m_activeTransfers.isEmpty(),2000);
    QTRY_COMPARE_WITH_TIMEOUT(first.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,2000);
    QTRY_COMPARE_WITH_TIMEOUT(second.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,2000);

    first.m_reconnectTimer->stop();
    second.m_reconnectTimer->stop();
    firstIdentity={};
    secondIdentity={};

    QVERIFY(server.startServer(port));
    first.attemptReconnect();
    second.attemptReconnect();
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),2,2000);
    QTRY_VERIFY_WITH_TIMEOUT(firstIdentity.selfId!=0,2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondIdentity.selfId!=0,2000);
    QVERIFY(firstIdentity.selfId!=oldFirstId);
    QVERIFY(secondIdentity.selfId!=oldSecondId);
    QVERIFY(server.m_activeTransfers.isEmpty());

    QSignalSpy staleChunkSpy(&second,&ConnectionManager::fileChunkReceived);
    QVERIFY(first.sendFileChunkRequest(transferIdA,0,QByteArray("old!")));
    QTest::qWait(50);
    QCOMPARE(staleChunkSpy.count(),0);
    QVERIFY(server.m_activeTransfers.isEmpty());

    QSignalSpy textSpy(&second,&ConnectionManager::textMessageReceived);
    QVERIFY(first.sendTextMessage(secondIdentity.selfId,
                                  QStringLiteral("after reconnect")));
    QTRY_COMPARE_WITH_TIMEOUT(textSpy.count(),1,2000);

    QSignalSpy startSpy(&second,&ConnectionManager::fileStartReceived);
    QSignalSpy endSpy(&second,&ConnectionManager::fileEndReceived);
    QSignalSpy completeSpy(&first,&ConnectionManager::fileCompleteReceived);
    QVERIFY(first.sendFileStartRequest(
        secondIdentity.selfId,transferIdB,QStringLiteral("new-empty.bin"),0));
    QTRY_COMPARE_WITH_TIMEOUT(startSpy.count(),1,2000);
    QVERIFY(first.sendFileEndRequest(transferIdB));
    QTRY_COMPARE_WITH_TIMEOUT(endSpy.count(),1,2000);
    QVERIFY(second.sendFileCompleteRequest(transferIdB));
    QTRY_COMPARE_WITH_TIMEOUT(completeSpy.count(),1,2000);
    QVERIFY(server.m_activeTransfers.isEmpty());

    disconnectClient(first);
    disconnectClient(second);
    server.stopServer();
}

void Stage15IntegrationTests::heartbeatAndServerWatchdogInteractCorrectly()
{
    TcpServer server;
    QVERIFY(server.startServer(0));
    const quint16 port=server.m_server->serverPort();

    ConnectionManager client;
    ClientIdentity identity;
    trackIdentity(client,identity);
    client.connectToServer(QStringLiteral("127.0.0.1"),port);
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),1,2000);
    QTRY_VERIFY_WITH_TIMEOUT(identity.selfId!=0,2000);

    client.m_lastServerActivityMs=-1;
    server.m_sessions[identity.selfId].lastActivityMs=-1;
    client.handleHeartbeatTimer();
    QTRY_VERIFY_WITH_TIMEOUT(client.m_lastServerActivityMs>=0,2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        server.m_sessions.value(identity.selfId).lastActivityMs>=0,2000);

    server.m_sessions[identity.selfId].readPaused=true;
    server.m_sessions[identity.selfId].lastActivityMs=
        server.m_activityClock.elapsed()-TcpServer::CLIENT_TIMEOUT_MS-1;
    server.checkClientTimeouts();
    QCoreApplication::processEvents();
    QVERIFY(server.m_sessions.contains(identity.selfId));

    server.m_sessions[identity.selfId].readPaused=false;
    server.checkClientTimeouts();
    QTRY_VERIFY_WITH_TIMEOUT(!server.m_sessions.contains(identity.selfId),2000);
    QTRY_COMPARE_WITH_TIMEOUT(client.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,2000);

    client.disconnectFromServer();
    server.stopServer();
}

void Stage15IntegrationTests::malformedTextRequestDisconnectsClient()
{
    TcpServer server;
    QVERIFY(server.startServer(0));

    ConnectionManager client;
    client.connectToServer(
        QStringLiteral("127.0.0.1"),server.m_server->serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(server.m_sessions.size(),1,2000);

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::TextSendRequest;
    packet.body=QByteArrayLiteral("{\"receiverId\":");
    QVERIFY(client.m_socket->write(Protocol::PacketCodec::encode(packet))>0);

    QTRY_VERIFY_WITH_TIMEOUT(server.m_sessions.isEmpty(),2000);
    QTRY_COMPARE_WITH_TIMEOUT(client.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,2000);

    client.disconnectFromServer();
    server.stopServer();
}

void Stage15IntegrationTests::malformedServerTextStopsReconnect()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost,0));

    ConnectionManager client;
    client.connectToServer(
        QStringLiteral("127.0.0.1"),listener.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(client.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(),1000);
    QTcpSocket *peer=listener.nextPendingConnection();
    QVERIFY(peer);

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::TextMessage;
    packet.body=QByteArrayLiteral("{\"senderId\":");
    QVERIFY(peer->write(Protocol::PacketCodec::encode(packet))>0);
    QVERIFY(peer->waitForBytesWritten(1000));

    QTRY_VERIFY_WITH_TIMEOUT(!client.m_connectionDesired,1000);
    QTRY_COMPARE_WITH_TIMEOUT(client.m_connectionState,
                              ConnectionManager::ConnectionState::Disconnected,1000);
    QVERIFY(!client.m_reconnectTimer->isActive());

    peer->deleteLater();
}

void Stage15IntegrationTests::databaseHistorySurvivesManagerReopen()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString databasePath=temporaryDir.filePath(QStringLiteral("qtlink.db"));

    QString errorMessage;
    {
        DatabaseManager database(databasePath);
        QVERIFY2(database.initialize(errorMessage),qPrintable(errorMessage));
        QVERIFY2(database.saveMessage(
            MessageDirection::Sent,QStringLiteral("persistent message"),errorMessage),
            qPrintable(errorMessage));
    }

    QList<MessageRecord> messages;
    {
        DatabaseManager database(databasePath);
        QVERIFY2(database.initialize(errorMessage),qPrintable(errorMessage));
        QVERIFY2(database.loadMessages(messages,errorMessage),
                 qPrintable(errorMessage));
    }

    QCOMPARE(messages.size(),1);
    QCOMPARE(messages.first().direction,MessageDirection::Sent);
    QCOMPARE(messages.first().content,QStringLiteral("persistent message"));
}

void Stage15IntegrationTests::destructionWithPendingReconnectTimerIsSafe()
{
    QPointer<ConnectionManager> guard;
    {
        ConnectionManager *manager=new ConnectionManager;
        guard=manager;
        manager->m_connectionDesired=true;
        manager->scheduleReconnect();
        QVERIFY(manager->m_reconnectTimer->isActive());
        delete manager;
    }

    QVERIFY(guard.isNull());
    QCoreApplication::processEvents();
}

QTEST_GUILESS_MAIN(Stage15IntegrationTests)

#include "stage15_integration_tests.moc"
