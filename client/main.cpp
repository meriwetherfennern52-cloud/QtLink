#include "mainwindow.h"

#include <QApplication>
#include "packetcodec.h"
#include <QDebug>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Protocol::Packet originalPacket;

    originalPacket.type = Protocol::MessageType::TextMessage;
    originalPacket.body = QByteArray("hello");

    QByteArray encoded = Protocol::PacketCodec::encode(originalPacket);

    qDebug() << "encoded size:" << encoded.size();
    qDebug() << "encoded hex:" << encoded.toHex(' ');

    Protocol::Packet decodedPacket;

    bool success =
        Protocol::PacketCodec::decode(encoded, decodedPacket);

    qDebug() << "decode success:" << success;
    qDebug() << "decoded body:" << decodedPacket.body;

    QByteArray badMagic = encoded;
    badMagic[0] = 'X';
    Protocol::Packet packet1;
    qDebug() << "bad magic:"
             << Protocol::PacketCodec::decode(badMagic, packet1);

    QByteArray incomplete = encoded.left(encoded.size() - 2);

    Protocol::Packet packet2;
    qDebug() << "incomplete packet:"
             << Protocol::PacketCodec::decode(incomplete, packet2);

    MainWindow w;
    w.show();
    return QApplication::exec();
}
