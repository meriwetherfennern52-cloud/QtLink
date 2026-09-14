#include <QtTest>

#include "packet.h"
#include "packetcodec.h"
#include "packetparser.h"
#include "textmessagecodec.h"

class ProtocolTests : public QObject
{
    Q_OBJECT

private slots:
    void packetCodecRoundTrip();
    void parserStickyPackets();
    void parserPartialPacket();
    void parserCompletePlusPartialPacket();

    void parserRejectsInvalidMagic();
    void parserRejectsInvalidVersion();
    void parserRejectsInvalidMessageType();
    void parserRejectsOversizedBody();

    void textSendRequestRoundTrip_data();
    void textSendRequestRoundTrip();
    void textMessageRoundTrip();
    void textCodecRejectsInvalidJson();
    void textSendRequestRejectsMissingContent();
    void textSendRequestRejectsInvalidContentType();
    void textMessageRejectsMissingContent();
    void textMessageRejectsMissingSenderId();
    void textMessageRejectsInvalidSenderId_data();
    void textMessageRejectsInvalidSenderId();
    void textMessageRejectsInvalidContentType();
};

void ProtocolTests::packetCodecRoundTrip()
{
    Protocol::Packet originalPacket;
    originalPacket.type =
        Protocol::MessageType::TextSendRequest;
    originalPacket.body =
        QByteArray("hello");

    QByteArray data =
        Protocol::PacketCodec::encode(
            originalPacket);

    Protocol::Packet decodedPacket;

    const bool success =
        Protocol::PacketCodec::decode(
            data,
            decodedPacket);

    QVERIFY(success);

    QCOMPARE(
        decodedPacket.type,
        originalPacket.type);

    QCOMPARE(
        decodedPacket.body,
        originalPacket.body);
}

void ProtocolTests::parserStickyPackets()
{
    Protocol::Packet packetA;
    packetA.type =
        Protocol::MessageType::TextSendRequest;
    packetA.body = QByteArray("message A");

    Protocol::Packet packetB;
    packetB.type =
        Protocol::MessageType::TextSendRequest;
    packetB.body = QByteArray("message B");

    const QByteArray dataA =
        Protocol::PacketCodec::encode(packetA);

    const QByteArray dataB =
        Protocol::PacketCodec::encode(packetB);

    QByteArray buffer = dataA + dataB;

    Protocol::Packet parsedPacket;

    Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::Success);

    QCOMPARE(parsedPacket.type, packetA.type);
    QCOMPARE(parsedPacket.body, packetA.body);

    // 解析 A 后必须完整保留 B，防止粘包场景下误清空接收缓冲区。
    QCOMPARE(buffer, dataB);

    result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::Success);

    QCOMPARE(parsedPacket.type, packetB.type);
    QCOMPARE(parsedPacket.body, packetB.body);

    QVERIFY(buffer.isEmpty());
}

void ProtocolTests::parserPartialPacket()
{
    Protocol::Packet originalPacket;
    originalPacket.type =
        Protocol::MessageType::TextSendRequest;
    originalPacket.body =
        QByteArray("partial message");

    const QByteArray data =
        Protocol::PacketCodec::encode(originalPacket);

    const qsizetype splitPoint =
        data.size() - 3;

    QByteArray buffer =
        data.left(splitPoint);

    const QByteArray originalBuffer = buffer;

    Protocol::Packet parsedPacket;

    Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::NeedMoreData);

    QCOMPARE(buffer, originalBuffer);

    buffer.append(data.mid(splitPoint));

    result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::Success);

    QCOMPARE(
        parsedPacket.type,
        originalPacket.type);

    QCOMPARE(
        parsedPacket.body,
        originalPacket.body);

    QVERIFY(buffer.isEmpty());
}

void ProtocolTests::parserCompletePlusPartialPacket()
{
    Protocol::Packet packetA;
    packetA.type =
        Protocol::MessageType::TextSendRequest;
    packetA.body =
        QByteArray("message A");

    Protocol::Packet packetB;
    packetB.type =
        Protocol::MessageType::TextSendRequest;
    packetB.body =
        QByteArray("message B");

    const QByteArray dataA =
        Protocol::PacketCodec::encode(packetA);

    const QByteArray dataB =
        Protocol::PacketCodec::encode(packetB);

    const qsizetype splitPoint =
        Protocol::HEADER_SIZE + 2;

    const QByteArray partialB =
        dataB.left(splitPoint);

    QByteArray buffer =
        dataA + partialB;

    Protocol::Packet parsedPacket;

    Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::Success);

    QCOMPARE(parsedPacket.body, packetA.body);

    // A 被消费后，B 的残片必须完整保留，等待后续 TCP 数据继续拼接。
    QCOMPARE(buffer, partialB);

    const QByteArray bufferBeforeRetry = buffer;

    result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::NeedMoreData);

    QCOMPARE(buffer, bufferBeforeRetry);

    buffer.append(dataB.mid(splitPoint));

    result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::Success);

    QCOMPARE(parsedPacket.body, packetB.body);

    QVERIFY(buffer.isEmpty());
}

void ProtocolTests::parserRejectsInvalidMagic()
{
    Protocol::Packet packet;
    packet.type =
        Protocol::MessageType::TextSendRequest;
    packet.body = QByteArray("hello");

    QByteArray buffer =
        Protocol::PacketCodec::encode(packet);

    buffer[0] = 'X';

    const QByteArray originalBuffer = buffer;

    Protocol::Packet parsedPacket;

    const Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::InvalidData);

    QCOMPARE(buffer, originalBuffer);
}

void ProtocolTests::parserRejectsInvalidVersion()
{
    Protocol::Packet packet;
    packet.type =
        Protocol::MessageType::TextSendRequest;
    packet.body = QByteArray("hello");

    QByteArray buffer =
        Protocol::PacketCodec::encode(packet);

    buffer[Protocol::MAGIC_SIZE] =
        static_cast<char>(Protocol::VERSION + 1);

    const QByteArray originalBuffer = buffer;

    Protocol::Packet parsedPacket;

    const Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::InvalidData);

    QCOMPARE(buffer, originalBuffer);
}

void ProtocolTests::parserRejectsInvalidMessageType()
{
    Protocol::Packet packet;
    packet.type =
        Protocol::MessageType::TextSendRequest;
    packet.body = QByteArray("hello");

    QByteArray buffer =
        Protocol::PacketCodec::encode(packet);

    // MessageType 紧跟在 Magic 和 Version 后，将两个字节都改为 0。
    const qsizetype typeOffset =
        Protocol::MAGIC_SIZE + sizeof(quint8);

    buffer[typeOffset] = 0;
    buffer[typeOffset + 1] = 0;

    const QByteArray originalBuffer = buffer;

    Protocol::Packet parsedPacket;

    const Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::InvalidData);

    QCOMPARE(buffer, originalBuffer);
}

void ProtocolTests::parserRejectsOversizedBody()
{
    QByteArray buffer;

    QDataStream stream(
        &buffer,
        QIODevice::WriteOnly);

    stream.setByteOrder(QDataStream::BigEndian);

    stream.writeRawData(
        Protocol::MAGIC,
        Protocol::MAGIC_SIZE);

    stream << Protocol::VERSION;

    stream << static_cast<quint16>(
        Protocol::MessageType::TextSendRequest);

    stream << static_cast<quint32>(
        Protocol::MAX_BODY_SIZE + 1);

    const QByteArray originalBuffer = buffer;

    Protocol::Packet parsedPacket;

    const Protocol::ParseResult result =
        Protocol::PacketParser::tryParse(
            buffer,
            parsedPacket);

    QVERIFY(
        result == Protocol::ParseResult::InvalidData);

    QCOMPARE(buffer, originalBuffer);
}



void ProtocolTests::textSendRequestRoundTrip_data()
{
    QTest::addColumn<QString>("originalContent");

    QTest::newRow("plain-text")
        << QStringLiteral("hello");
    QTest::newRow("unicode-and-escapes")
        << QString::fromUtf8(u8"中文 😀 \"quoted\" \\\n下一行");
}

void ProtocolTests::textSendRequestRoundTrip()
{
    QFETCH(QString, originalContent);

    const QByteArray data =
        Protocol::TextMessageCodec::encodeSendRequest(
            originalContent);

    QString content;

    QVERIFY(
        Protocol::TextMessageCodec::decodeSendRequest(
            data,
            content));

    QCOMPARE(content, originalContent);
}

void ProtocolTests::textMessageRoundTrip()
{
    const quint32 originalSenderId = 123;
    const QString originalContent =
        QStringLiteral("hello");

    const QByteArray data =
        Protocol::TextMessageCodec::encodeTextMessage(
            originalSenderId,
            originalContent);

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));

    QCOMPARE(senderId, originalSenderId);
    QCOMPARE(content, originalContent);
}

void ProtocolTests::textCodecRejectsInvalidJson()
{
    const QByteArray data("{\"content\":");

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            content));

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

void ProtocolTests::textSendRequestRejectsMissingContent()
{
    const QByteArray data("{}");
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            content));
}

void ProtocolTests::textSendRequestRejectsInvalidContentType()
{
    const QByteArray data(R"({"content":123})");
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            content));
}

void ProtocolTests::textMessageRejectsMissingContent()
{
    const QByteArray data(
        R"({"senderId":1})");

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

void ProtocolTests::textMessageRejectsMissingSenderId()
{
    const QByteArray data(R"({"content":"hello"})");

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

void ProtocolTests::textMessageRejectsInvalidSenderId_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("zero")
        << QByteArray(R"({"senderId":0,"content":"hello"})");
    QTest::newRow("negative")
        << QByteArray(R"({"senderId":-1,"content":"hello"})");
    QTest::newRow("fractional")
        << QByteArray(R"({"senderId":1.5,"content":"hello"})");
    QTest::newRow("above-quint32-max")
        << QByteArray(R"({"senderId":4294967296,"content":"hello"})");
}

void ProtocolTests::textMessageRejectsInvalidSenderId()
{
    QFETCH(QByteArray, data);

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

void ProtocolTests::textMessageRejectsInvalidContentType()
{
    const QByteArray data(R"({"senderId":1,"content":123})");

    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

QTEST_APPLESS_MAIN(ProtocolTests)

#include "protocol_tests.moc"
