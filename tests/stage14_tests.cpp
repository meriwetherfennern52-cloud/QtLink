#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#define private public
#include "connectionmanager.h"
#include "filetransfermanager.h"
#undef private

#include "packetcodec.h"

namespace {

quint16 unusedLocalPort()
{
    QTcpServer listener;
    if(!listener.listen(QHostAddress::LocalHost,0)){
        return 0;
    }

    return listener.serverPort();
}

QTcpSocket *acceptConnection(QTcpServer &listener)
{
    if(!listener.hasPendingConnections()
        &&!listener.waitForNewConnection(1000)){

        return nullptr;
    }

    return listener.nextPendingConnection();
}

}

class Stage14Tests:public QObject
{
    Q_OBJECT

private slots:
    void manualDisconnectStopsReconnect();
    void manualDisconnectRejectsLateConnectedSignal();
    void firstConnectionFailureSchedulesReconnect();
    void duplicateSchedulingIsPrevented();
    void exponentialDelayIncreasesAndCaps();
    void successfulConnectionResetsDelayAndReconnectUsesSavedEndpoint();
    void connectionStateTransitions();
    void heartbeatTimeoutPreservesReconnectIntent();
    void protocolErrorStopsReconnect();
    void fileTransferControlFailurePreservesReconnectIntent();
    void disconnectCleanupAndReconnectDoNotRestoreTransfers();
};

void Stage14Tests::manualDisconnectStopsReconnect()
{
    ConnectionManager manager;
    manager.m_connectionDesired=true;
    manager.m_host=QStringLiteral("127.0.0.1");
    manager.m_port=unusedLocalPort();
    QVERIFY(manager.m_port!=0);

    manager.scheduleReconnect();
    QVERIFY(manager.m_reconnectTimer->isActive());
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Reconnecting);

    manager.disconnectFromServer();

    QVERIFY(!manager.m_connectionDesired);
    QVERIFY(!manager.m_reconnectTimer->isActive());
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Disconnected);

    manager.attemptReconnect();
    QCOMPARE(manager.m_socket->state(),QAbstractSocket::UnconnectedState);
}

void Stage14Tests::manualDisconnectRejectsLateConnectedSignal()
{
    ConnectionManager manager;
    manager.m_connectionDesired=true;
    manager.setConnectionState(ConnectionManager::ConnectionState::Connecting);

    manager.disconnectFromServer();
    manager.handleSocketConnected();

    QVERIFY(!manager.m_connectionDesired);
    QVERIFY(!manager.m_heartbeatTimer->isActive());
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Disconnected);
}

void Stage14Tests::firstConnectionFailureSchedulesReconnect()
{
    const quint16 port=unusedLocalPort();
    QVERIFY(port!=0);

    ConnectionManager manager;
    manager.connectToServer(QStringLiteral("127.0.0.1"),port);

    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Connecting);
    QVERIFY(!manager.m_socket->waitForConnected(1000));
    QTRY_VERIFY_WITH_TIMEOUT(manager.m_reconnectTimer->isActive(),250);
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Reconnecting);
    QVERIFY(manager.m_connectionDesired);

    manager.disconnectFromServer();
}

void Stage14Tests::duplicateSchedulingIsPrevented()
{
    ConnectionManager manager;
    manager.m_connectionDesired=true;

    manager.scheduleReconnect();
    QCOMPARE(manager.m_reconnectTimer->interval(),1000);
    QCOMPARE(manager.m_reconnectDelayMs,2000);

    manager.scheduleReconnect();
    QCOMPARE(manager.m_reconnectTimer->interval(),1000);
    QCOMPARE(manager.m_reconnectDelayMs,2000);

    manager.disconnectFromServer();
}

void Stage14Tests::exponentialDelayIncreasesAndCaps()
{
    ConnectionManager manager;
    manager.m_connectionDesired=true;

    const QList<int> expectedDelays={1000,2000,4000,8000,16000,30000,30000};
    for(int expectedDelay:expectedDelays){
        manager.m_reconnectTimer->stop();
        manager.scheduleReconnect();
        QCOMPARE(manager.m_reconnectTimer->interval(),expectedDelay);
    }

    QCOMPARE(manager.m_reconnectDelayMs,
             ConnectionManager::MAX_RECONNECT_DELAY_MS);
    manager.disconnectFromServer();
}

void Stage14Tests::successfulConnectionResetsDelayAndReconnectUsesSavedEndpoint()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost,0));

    ConnectionManager manager;
    const QString host=QHostAddress(QHostAddress::LocalHost).toString();
    const quint16 port=listener.serverPort();
    manager.connectToServer(host,port);
    manager.m_reconnectDelayMs=16000;

    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *firstPeer=acceptConnection(listener);
    QVERIFY(firstPeer);
    QCOMPARE(manager.m_host,host);
    QCOMPARE(manager.m_port,port);
    QCOMPARE(manager.m_reconnectDelayMs,
             ConnectionManager::INITIAL_RECONNECT_DELAY_MS);
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Connected);

    firstPeer->abort();
    firstPeer->deleteLater();
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,1000);
    QCOMPARE(manager.m_reconnectDelayMs,2000);

    manager.m_reconnectTimer->stop();
    manager.attemptReconnect();
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Connecting);
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *secondPeer=acceptConnection(listener);
    QVERIFY(secondPeer);
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Connected);

    manager.disconnectFromServer();
    secondPeer->deleteLater();
}

void Stage14Tests::connectionStateTransitions()
{
    ConnectionManager manager;
    QSignalSpy stateSpy(&manager,&ConnectionManager::connectionStateChanged);
    manager.m_connectionDesired=true;

    manager.setConnectionState(ConnectionManager::ConnectionState::Connecting);
    manager.setConnectionState(ConnectionManager::ConnectionState::Connecting);
    QCOMPARE(stateSpy.count(),1);

    manager.scheduleReconnect();
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Reconnecting);

    manager.m_reconnectTimer->stop();
    manager.attemptReconnect();
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Connecting);

    manager.disconnectFromServer();
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Disconnected);
}

void Stage14Tests::heartbeatTimeoutPreservesReconnectIntent()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost,0));

    ConnectionManager manager;
    manager.connectToServer(
        QHostAddress(QHostAddress::LocalHost).toString(),listener.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *peer=acceptConnection(listener);
    QVERIFY(peer);

    manager.m_lastServerActivityMs=
        manager.m_connectionClock.elapsed()
        -ConnectionManager::SERVER_TIMEOUT_MS;
    manager.handleHeartbeatTimer();

    QVERIFY(manager.m_connectionDesired);
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,1000);
    QVERIFY(manager.m_reconnectTimer->isActive());

    manager.disconnectFromServer();
    peer->deleteLater();
}

void Stage14Tests::protocolErrorStopsReconnect()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost,0));

    ConnectionManager manager;
    manager.connectToServer(
        QHostAddress(QHostAddress::LocalHost).toString(),listener.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *peer=acceptConnection(listener);
    QVERIFY(peer);

    Protocol::Packet packet;
    packet.type=Protocol::MessageType::HeartbeatResponse;
    packet.body=QByteArrayLiteral("invalid");
    QVERIFY(peer->write(Protocol::PacketCodec::encode(packet))>0);
    QVERIFY(peer->waitForBytesWritten(1000));

    QTRY_VERIFY_WITH_TIMEOUT(!manager.m_connectionDesired,1000);
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_connectionState,
                              ConnectionManager::ConnectionState::Disconnected,1000);
    QVERIFY(!manager.m_reconnectTimer->isActive());

    peer->deleteLater();
}

void Stage14Tests::fileTransferControlFailurePreservesReconnectIntent()
{
    ConnectionManager manager;
    FileTransferManager transferManager(&manager);
    manager.m_connectionDesired=true;
    manager.m_host=QStringLiteral("127.0.0.1");
    manager.m_port=unusedLocalPort();
    QVERIFY(manager.m_port!=0);

    transferManager.m_sendState=FileTransferManager::SendState::Sending;
    transferManager.m_sendTransferId=
        QStringLiteral("550e8400-e29b-41d4-a716-446655440000");
    transferManager.abortOutgoingTransfer(
        Protocol::FileAbortReason::PeerDisconnected);

    QVERIFY(manager.m_connectionDesired);
    QVERIFY(manager.m_reconnectTimer->isActive());
    QCOMPARE(manager.m_connectionState,
             ConnectionManager::ConnectionState::Reconnecting);
    QCOMPARE(transferManager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(transferManager.m_sendTransferId.isEmpty());

    manager.disconnectFromServer();
}

void Stage14Tests::disconnectCleanupAndReconnectDoNotRestoreTransfers()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost,0));

    ConnectionManager manager;
    FileTransferManager transferManager(&manager);
    manager.connectToServer(
        QHostAddress(QHostAddress::LocalHost).toString(),listener.serverPort());
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *firstPeer=acceptConnection(listener);
    QVERIFY(firstPeer);

    transferManager.m_sendState=FileTransferManager::SendState::Finishing;
    transferManager.m_sendTransferId=
        QStringLiteral("550e8400-e29b-41d4-a716-446655440000");

    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    QFile *partialFile=new QFile(
        temporaryDir.filePath(QStringLiteral("partial.bin")),&transferManager);
    QVERIFY(partialFile->open(QIODevice::WriteOnly|QIODevice::NewOnly));
    FileTransferManager::IncomingTransfer incomingTransfer;
    incomingTransfer.file=partialFile;
    incomingTransfer.fileSize=10;
    transferManager.m_incomingTransfers.insert(
        QStringLiteral("550e8400-e29b-41d4-a716-446655440001"),incomingTransfer);

    firstPeer->abort();
    firstPeer->deleteLater();
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_connectionState,
                              ConnectionManager::ConnectionState::Reconnecting,1000);
    QCOMPARE(transferManager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(transferManager.m_sendTransferId.isEmpty());
    QVERIFY(transferManager.m_incomingTransfers.isEmpty());

    manager.m_reconnectTimer->stop();
    manager.attemptReconnect();
    QTRY_COMPARE_WITH_TIMEOUT(manager.m_socket->state(),
                              QAbstractSocket::ConnectedState,1000);
    QTcpSocket *secondPeer=acceptConnection(listener);
    QVERIFY(secondPeer);
    QCOMPARE(transferManager.m_sendState,FileTransferManager::SendState::Idle);
    QVERIFY(transferManager.m_sendTransferId.isEmpty());
    QVERIFY(transferManager.m_incomingTransfers.isEmpty());

    manager.disconnectFromServer();
    secondPeer->deleteLater();
}

QTEST_MAIN(Stage14Tests)

#include "stage14_tests.moc"
