# QtLink Protocol

## Packet Format

Packet = Header + Body

Header fields:

1. Magic Number
   - 内容：QLNK
   - 大小：4 bytes
   - 用于识别 QtLink 数据包并进行基本合法性检查

2. Version
   - 类型：quint8
   - 大小：1 byte
   - 当前协议版本：1
   - 用于识别协议格式版本及兼容性

3. Message Type
   - 类型：quint16
   - 大小：2 bytes
   - 表示当前 Packet 的消息类型
   - 当前第一版支持：
     - 1：TextMessage

4. Body Length
   - 类型：quint32
   - 大小：4 bytes（最大4GB）
   - 表示当前消息体 Body 的字节长度

5. Body
   - 实际消息内容

## Header Layout

| Field        | Type               | Size    |
| ------------ | ------------------ | ------- |
| Magic Number | 4-byte fixed value | 4 bytes |
| Version      | quint8             | 1 byte  |
| Message Type | quint16            | 2 bytes |
| Body Length  | quint32            | 4 bytes |

Header Size = 11 bytes

## Byte Order

All multi-byte integer fields use Big Endian (network byte order).
所有多字节整数字段统一使用大端字节序（网络字节序）。