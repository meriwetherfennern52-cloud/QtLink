#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

#include "packet.h"
#include "packetcodec.h"
#include "packetparser.h"
#include "textmessagecodec.h"
#include "onlineclientlistcodec.h"
#include "filetransfercontrolcodec.h"
#include "filechunkcodec.h"

namespace {

const QString fileTransferId =
    QStringLiteral("550e8400-e29b-41d4-a716-446655440000");

void addInvalidFileStartRows(const QString &idField)
{
    QTest::addColumn<QByteArray>("data");

    QJsonObject original;
    original[idField] = 2;
    original["transferId"] = fileTransferId;
    original["fileName"] = QStringLiteral("report.pdf");
    original["fileSize"] = 1024;

    auto addRow = [&original](const char *name,
                             const QString &field,
                             const QJsonValue &value)
    {
        QJsonObject object = original;
        if(value.isUndefined()){
            object.remove(field);
        }else{
            object[field] = value;
        }
        QTest::newRow(name)
            << QJsonDocument(object).toJson(QJsonDocument::Compact);
    };

    QTest::newRow("invalid-json") << QByteArray("{");
    QTest::newRow("root-not-object") << QByteArray("[]");
    addRow("missing-id", idField, QJsonValue(QJsonValue::Undefined));
    addRow("id-zero", idField, 0);
    addRow("id-negative", idField, -1);
    addRow("id-fractional", idField, 1.5);
    addRow("id-above-quint32-max", idField, QJsonValue(qint64(4294967296)));
    addRow("id-string", idField, QStringLiteral("2"));
    addRow("missing-transfer-id", QStringLiteral("transferId"),
           QJsonValue(QJsonValue::Undefined));
    addRow("transfer-id-number", QStringLiteral("transferId"), 123);
    addRow("transfer-id-empty", QStringLiteral("transferId"), QStringLiteral(""));
    addRow("transfer-id-invalid-uuid", QStringLiteral("transferId"),
           QStringLiteral("not-a-uuid"));
    addRow("transfer-id-null-uuid", QStringLiteral("transferId"),
           QStringLiteral("00000000-0000-0000-0000-000000000000"));
    addRow("missing-file-name", QStringLiteral("fileName"),
           QJsonValue(QJsonValue::Undefined));
    addRow("file-name-number", QStringLiteral("fileName"), 123);
    addRow("file-name-empty", QStringLiteral("fileName"), QStringLiteral(""));
    addRow("missing-file-size", QStringLiteral("fileSize"),
           QJsonValue(QJsonValue::Undefined));
    addRow("file-size-fractional", QStringLiteral("fileSize"), 1.5);
    addRow("file-size-negative", QStringLiteral("fileSize"), -1);
    addRow("file-size-string", QStringLiteral("fileSize"), QStringLiteral("1024"));
}

}

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
    void textSendRequestRejectsInvalidReceiverId_data();
    void textSendRequestRejectsInvalidReceiverId();
    void textMessageRejectsMissingContent();
    void textMessageRejectsMissingSenderId();
    void textMessageRejectsInvalidSenderId_data();
    void textMessageRejectsInvalidSenderId();
    void textMessageRejectsInvalidContentType();

    void onlineClientListRoundTrip();
    void onlineClientListRejectsInvalidData_data();
    void onlineClientListRejectsInvalidData();

    void fileStartRequestRoundTrip_data();
    void fileStartRequestRoundTrip();
    void fileStartRequestRejectsInvalidData_data();
    void fileStartRequestRejectsInvalidData();
    void fileStartRoundTrip_data();
    void fileStartRoundTrip();
    void fileStartRejectsInvalidData_data();
    void fileStartRejectsInvalidData();
    void fileControlUuidNormalization_data();
    void fileControlUuidNormalization();
    void fileControlEncodersRejectInvalidUuid_data();
    void fileControlEncodersRejectInvalidUuid();
    void fileEndRoundTrip();
    void fileEndRejectsInvalidData_data();
    void fileEndRejectsInvalidData();
    void fileChunkRoundTrip_data();
    void fileChunkRoundTrip();
    void fileChunkRejectsInvalidData_data();
    void fileChunkRejectsInvalidData();
    void fileChunkEncodeRejectsInvalidUuid_data();
    void fileChunkEncodeRejectsInvalidUuid();
    void fileMessageTypesAreValid_data();
    void fileMessageTypesAreValid();
    void messageTypeRejectsUndefinedValue();
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
    const quint32 originalReceiverId = 2;

    const QByteArray data =
        Protocol::TextMessageCodec::encodeSendRequest(
            originalReceiverId,
            originalContent);

    quint32 receiverId = 0;
    QString content;

    QVERIFY(
        Protocol::TextMessageCodec::decodeSendRequest(
            data,
            receiverId,
            content));

    QCOMPARE(receiverId, originalReceiverId);
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

    quint32 receiverId = 0;
    quint32 senderId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            receiverId,
            content));

    QVERIFY(
        !Protocol::TextMessageCodec::decodeTextMessage(
            data,
            senderId,
            content));
}

void ProtocolTests::textSendRequestRejectsMissingContent()
{
    const QByteArray data(R"({"receiverId":2})");
    quint32 receiverId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            receiverId,
            content));
}

void ProtocolTests::textSendRequestRejectsInvalidContentType()
{
    const QByteArray data(R"({"receiverId":2,"content":123})");
    quint32 receiverId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            receiverId,
            content));
}

void ProtocolTests::textSendRequestRejectsInvalidReceiverId_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("zero")
        << QByteArray(R"({"receiverId":0,"content":"hello"})");
    QTest::newRow("negative")
        << QByteArray(R"({"receiverId":-1,"content":"hello"})");
    QTest::newRow("fractional")
        << QByteArray(R"({"receiverId":1.5,"content":"hello"})");
    QTest::newRow("above-quint32-max")
        << QByteArray(R"({"receiverId":4294967296,"content":"hello"})");
    QTest::newRow("missing")
        << QByteArray(R"({"content":"hello"})");
    QTest::newRow("string")
        << QByteArray(R"({"receiverId":"2","content":"hello"})");
}

void ProtocolTests::textSendRequestRejectsInvalidReceiverId()
{
    QFETCH(QByteArray, data);

    quint32 receiverId = 0;
    QString content;

    QVERIFY(
        !Protocol::TextMessageCodec::decodeSendRequest(
            data,
            receiverId,
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

void ProtocolTests::onlineClientListRoundTrip()
{
    const quint32 originalSelfId = 2;
    const QList<quint32> originalClientIds = {1, 2, 3};

    const QByteArray data =
        Protocol::OnlineClientListCodec::encode(
            originalSelfId,
            originalClientIds);

    quint32 selfId = 0;
    QList<quint32> clientIds;

    QVERIFY(
        Protocol::OnlineClientListCodec::decode(
            data,
            selfId,
            clientIds));

    QCOMPARE(selfId, originalSelfId);
    QCOMPARE(clientIds, originalClientIds);
}

void ProtocolTests::onlineClientListRejectsInvalidData_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("invalid-json")
        << QByteArray(R"({"selfId":2,"clientIds":[)");
    QTest::newRow("missing-self-id")
        << QByteArray(R"({"clientIds":[1,2,3]})");
    QTest::newRow("missing-client-ids")
        << QByteArray(R"({"selfId":2})");
    QTest::newRow("self-id-string")
        << QByteArray(R"({"selfId":"2","clientIds":[1,2,3]})");
    QTest::newRow("client-ids-not-array")
        << QByteArray(R"({"selfId":2,"clientIds":2})");
    QTest::newRow("client-id-string")
        << QByteArray(R"({"selfId":2,"clientIds":[1,2,"3"]})");
    QTest::newRow("client-id-zero")
        << QByteArray(R"({"selfId":2,"clientIds":[1,2,0]})");
    QTest::newRow("client-id-negative")
        << QByteArray(R"({"selfId":2,"clientIds":[1,2,-1]})");
    QTest::newRow("client-id-fractional")
        << QByteArray(R"({"selfId":2,"clientIds":[1,2,1.5]})");
    QTest::newRow("client-id-above-quint32-max")
        << QByteArray(R"({"selfId":2,"clientIds":[1,2,4294967296]})");
    QTest::newRow("self-id-not-in-list")
        << QByteArray(R"({"selfId":2,"clientIds":[1,3]})");
}

void ProtocolTests::onlineClientListRejectsInvalidData()
{
    QFETCH(QByteArray, data);

    quint32 selfId = 0;
    QList<quint32> clientIds;

    QVERIFY(
        !Protocol::OnlineClientListCodec::decode(
            data,
            selfId,
            clientIds));
}

void ProtocolTests::fileStartRequestRoundTrip_data()
{
    QTest::addColumn<qint64>("originalFileSize");

    QTest::newRow("ten-megabytes") << qint64(10485760);
    QTest::newRow("zero-byte-file") << qint64(0);
}

void ProtocolTests::fileStartRequestRoundTrip()
{
    QFETCH(qint64, originalFileSize);
    const quint32 originalReceiverId = 2;
    const QString originalFileName = QStringLiteral("report.pdf");
    const QByteArray data =
        Protocol::FileTransferControlCodec::encodeStartRequest(
            originalReceiverId, fileTransferId, originalFileName, originalFileSize);
    QVERIFY(!data.isEmpty());

    quint32 receiverId = 0;
    QString transferId;
    QString fileName;
    qint64 fileSize = -1;
    QVERIFY(
        Protocol::FileTransferControlCodec::decodeStartRequest(
            data, receiverId, transferId, fileName, fileSize));

    QCOMPARE(receiverId, originalReceiverId);
    QCOMPARE(transferId, fileTransferId);
    QCOMPARE(fileName, originalFileName);
    QCOMPARE(fileSize, originalFileSize);
}

void ProtocolTests::fileStartRequestRejectsInvalidData_data()
{
    addInvalidFileStartRows(QStringLiteral("receiverId"));
}

void ProtocolTests::fileStartRequestRejectsInvalidData()
{
    QFETCH(QByteArray, data);
    quint32 receiverId = 99;
    QString transferId = QStringLiteral("unchanged-transfer");
    QString fileName = QStringLiteral("unchanged-name");
    qint64 fileSize = 99;

    QVERIFY(
        !Protocol::FileTransferControlCodec::decodeStartRequest(
            data, receiverId, transferId, fileName, fileSize));

    QCOMPARE(receiverId, quint32(99));
    QCOMPARE(transferId, QStringLiteral("unchanged-transfer"));
    QCOMPARE(fileName, QStringLiteral("unchanged-name"));
    QCOMPARE(fileSize, qint64(99));
}

void ProtocolTests::fileStartRoundTrip_data()
{
    QTest::addColumn<qint64>("originalFileSize");

    QTest::newRow("one-kilobyte") << qint64(1024);
    QTest::newRow("zero-byte-file") << qint64(0);
}

void ProtocolTests::fileStartRoundTrip()
{
    QFETCH(qint64, originalFileSize);
    const quint32 originalSenderId = 1;
    const QString originalFileName = QStringLiteral("report.pdf");
    const QByteArray data =
        Protocol::FileTransferControlCodec::encodeStart(
            originalSenderId, fileTransferId, originalFileName, originalFileSize);
    QVERIFY(!data.isEmpty());

    quint32 senderId = 0;
    QString transferId;
    QString fileName;
    qint64 fileSize = -1;
    QVERIFY(
        Protocol::FileTransferControlCodec::decodeStart(
            data, senderId, transferId, fileName, fileSize));

    QCOMPARE(senderId, originalSenderId);
    QCOMPARE(transferId, fileTransferId);
    QCOMPARE(fileName, originalFileName);
    QCOMPARE(fileSize, originalFileSize);
}

void ProtocolTests::fileStartRejectsInvalidData_data()
{
    addInvalidFileStartRows(QStringLiteral("senderId"));
}

void ProtocolTests::fileStartRejectsInvalidData()
{
    QFETCH(QByteArray, data);
    quint32 senderId = 99;
    QString transferId = QStringLiteral("unchanged-transfer");
    QString fileName = QStringLiteral("unchanged-name");
    qint64 fileSize = 99;

    QVERIFY(
        !Protocol::FileTransferControlCodec::decodeStart(
            data, senderId, transferId, fileName, fileSize));

    QCOMPARE(senderId, quint32(99));
    QCOMPARE(transferId, QStringLiteral("unchanged-transfer"));
    QCOMPARE(fileName, QStringLiteral("unchanged-name"));
    QCOMPARE(fileSize, qint64(99));
}

void ProtocolTests::fileControlUuidNormalization_data()
{
    QTest::addColumn<QString>("inputTransferId");

    QTest::newRow("braced")
        << QStringLiteral("{550e8400-e29b-41d4-a716-446655440000}");
    QTest::newRow("uppercase-braced")
        << QStringLiteral("{550E8400-E29B-41D4-A716-446655440000}");
}

void ProtocolTests::fileControlUuidNormalization()
{
    QFETCH(QString, inputTransferId);
    const QList<QByteArray> encodedBodies = {
        Protocol::FileTransferControlCodec::encodeStartRequest(
            2, inputTransferId, QStringLiteral("report.pdf"), 1024),
        Protocol::FileTransferControlCodec::encodeStart(
            1, inputTransferId, QStringLiteral("report.pdf"), 1024),
        Protocol::FileTransferControlCodec::encodeEnd(inputTransferId)
    };

    for(qsizetype index = 0; index < encodedBodies.size(); ++index){
        const QByteArray &encoded = encodedBodies.at(index);
        QVERIFY(!encoded.isEmpty());
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(encoded, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());
        QJsonObject object = document.object();
        QCOMPARE(object.value("transferId").toString(), fileTransferId);

        // 分别验证编码和解码的规范化，避免两端缺陷互相掩盖。
        object["transferId"] = inputTransferId;
        const QByteArray raw = QJsonDocument(object).toJson(QJsonDocument::Compact);
        for(const QByteArray &data : {encoded, raw}){
            quint32 clientId = 0;
            QString transferId;
            QString fileName;
            qint64 fileSize = -1;
            if(index == 0){
                QVERIFY(Protocol::FileTransferControlCodec::decodeStartRequest(
                    data, clientId, transferId, fileName, fileSize));
                QCOMPARE(clientId, quint32(2));
            }else if(index == 1){
                QVERIFY(Protocol::FileTransferControlCodec::decodeStart(
                    data, clientId, transferId, fileName, fileSize));
                QCOMPARE(clientId, quint32(1));
            }else{
                QVERIFY(Protocol::FileTransferControlCodec::decodeEnd(data, transferId));
            }
            QCOMPARE(transferId, fileTransferId);
        }
    }
}

void ProtocolTests::fileControlEncodersRejectInvalidUuid_data()
{
    QTest::addColumn<QString>("transferId");

    QTest::newRow("empty") << QStringLiteral("");
    QTest::newRow("invalid") << QStringLiteral("not-a-uuid");
    QTest::newRow("null") << QStringLiteral("00000000-0000-0000-0000-000000000000");
}

void ProtocolTests::fileControlEncodersRejectInvalidUuid()
{
    QFETCH(QString, transferId);

    QVERIFY(Protocol::FileTransferControlCodec::encodeStartRequest(
        2, transferId, QStringLiteral("report.pdf"), 1024).isEmpty());
    QVERIFY(Protocol::FileTransferControlCodec::encodeStart(
        1, transferId, QStringLiteral("report.pdf"), 1024).isEmpty());
    QVERIFY(Protocol::FileTransferControlCodec::encodeEnd(transferId).isEmpty());
}

void ProtocolTests::fileEndRoundTrip()
{
    const QByteArray data =
        Protocol::FileTransferControlCodec::encodeEnd(fileTransferId);
    QVERIFY(!data.isEmpty());

    QString transferId;
    QVERIFY(Protocol::FileTransferControlCodec::decodeEnd(data, transferId));
    QCOMPARE(transferId, fileTransferId);
}

void ProtocolTests::fileEndRejectsInvalidData_data()
{
    QTest::addColumn<QByteArray>("data");

    QTest::newRow("invalid-json") << QByteArray(R"({"transferId":)");
    QTest::newRow("root-not-object") << QByteArray("[]");
    QTest::newRow("missing-transfer-id") << QByteArray("{}");
    QTest::newRow("transfer-id-number") << QByteArray(R"({"transferId":123})");
    QTest::newRow("transfer-id-empty") << QByteArray(R"({"transferId":""})");
    QTest::newRow("transfer-id-invalid-uuid")
        << QByteArray(R"({"transferId":"not-a-uuid"})");
    QTest::newRow("transfer-id-null-uuid")
        << QByteArray(R"({"transferId":"00000000-0000-0000-0000-000000000000"})");
}

void ProtocolTests::fileEndRejectsInvalidData()
{
    QFETCH(QByteArray, data);
    QString transferId = QStringLiteral("unchanged-transfer");

    QVERIFY(!Protocol::FileTransferControlCodec::decodeEnd(data, transferId));
    QCOMPARE(transferId, QStringLiteral("unchanged-transfer"));
}

void ProtocolTests::fileChunkRoundTrip_data()
{
    QTest::addColumn<qint64>("originalOffset");
    QTest::addColumn<QByteArray>("originalChunkData");

    QTest::newRow("binary-at-zero")
        << qint64(0) << QByteArray::fromHex("41004280ff7f");
    QTest::newRow("small-chunk-at-nonzero-offset")
        << qint64(65536) << QByteArray::fromHex("00");
}

void ProtocolTests::fileChunkRoundTrip()
{
    QFETCH(qint64, originalOffset);
    QFETCH(QByteArray, originalChunkData);
    const QByteArray data =
        Protocol::FileChunkCodec::encode(
            fileTransferId, originalOffset, originalChunkData);
    QVERIFY(!data.isEmpty());
    QCOMPARE(data.size(), qsizetype(24) + originalChunkData.size());

    QString transferId;
    qint64 offset = -1;
    QByteArray chunkData;
    QVERIFY(
        Protocol::FileChunkCodec::decode(
            data, transferId, offset, chunkData));

    QCOMPARE(transferId, fileTransferId);
    QCOMPARE(offset, originalOffset);
    QCOMPARE(chunkData, originalChunkData);
}

void ProtocolTests::fileChunkRejectsInvalidData_data()
{
    QTest::addColumn<QByteArray>("data");

    // 16 字节 RFC 4122 UUID + 8 字节大端 offset，独立于被测编码器构造。
    const QByteArray uuid = QByteArray::fromHex("550e8400e29b41d4a716446655440000");
    const QByteArray zeroOffset(8, '\0');
    const QByteArray metadata = uuid + zeroOffset;
    const QByteArray chunkData = QByteArray::fromHex("4100ff");

    QTest::newRow("short-metadata") << metadata.left(23);
    QTest::newRow("metadata-without-chunk") << metadata;
    QTest::newRow("null-uuid")
        << (QByteArray(16, '\0') + zeroOffset + chunkData);
    QTest::newRow("negative-offset")
        << (uuid + QByteArray::fromHex("ffffffffffffffff") + chunkData);
}

void ProtocolTests::fileChunkRejectsInvalidData()
{
    QFETCH(QByteArray, data);
    QString transferId = QStringLiteral("unchanged-transfer");
    qint64 offset = 99;
    QByteArray chunkData("unchanged-chunk");

    QVERIFY(
        !Protocol::FileChunkCodec::decode(
            data, transferId, offset, chunkData));

    QCOMPARE(transferId, QStringLiteral("unchanged-transfer"));
    QCOMPARE(offset, qint64(99));
    QCOMPARE(chunkData, QByteArray("unchanged-chunk"));
}

void ProtocolTests::fileChunkEncodeRejectsInvalidUuid_data()
{
    QTest::addColumn<QString>("transferId");

    QTest::newRow("empty") << QStringLiteral("");
    QTest::newRow("invalid") << QStringLiteral("not-a-uuid");
    QTest::newRow("null") << QStringLiteral("00000000-0000-0000-0000-000000000000");
}

void ProtocolTests::fileChunkEncodeRejectsInvalidUuid()
{
    QFETCH(QString, transferId);

    QVERIFY(Protocol::FileChunkCodec::encode(
        transferId, 0, QByteArray::fromHex("4100ff")).isEmpty());
}

void ProtocolTests::fileMessageTypesAreValid_data()
{
    QTest::addColumn<quint16>("typeValue");
    QTest::addColumn<quint16>("expectedValue");

    QTest::newRow("FileStartRequest")
        << static_cast<quint16>(Protocol::MessageType::FileStartRequest) << quint16(4);
    QTest::newRow("FileStart")
        << static_cast<quint16>(Protocol::MessageType::FileStart) << quint16(5);
    QTest::newRow("FileChunkRequest")
        << static_cast<quint16>(Protocol::MessageType::FileChunkRequest) << quint16(6);
    QTest::newRow("FileChunk")
        << static_cast<quint16>(Protocol::MessageType::FileChunk) << quint16(7);
    QTest::newRow("FileEndRequest")
        << static_cast<quint16>(Protocol::MessageType::FileEndRequest) << quint16(8);
    QTest::newRow("FileEnd")
        << static_cast<quint16>(Protocol::MessageType::FileEnd) << quint16(9);
}

void ProtocolTests::fileMessageTypesAreValid()
{
    QFETCH(quint16, typeValue);
    QFETCH(quint16, expectedValue);

    QCOMPARE(typeValue, expectedValue);
    QVERIFY(Protocol::isValidMessageType(typeValue));
}

void ProtocolTests::messageTypeRejectsUndefinedValue()
{
    QVERIFY(!Protocol::isValidMessageType(quint16(10)));
}

QTEST_APPLESS_MAIN(ProtocolTests)

#include "protocol_tests.moc"
