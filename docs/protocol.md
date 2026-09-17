# QtLink Protocol Specification

## 1. Overview

QtLink 使用 TCP 作为传输层协议，并在 TCP 字节流之上定义自己的应用层协议。

由于 TCP 是面向字节流的协议，本身不保留应用层消息边界，因此 QtLink 需要自行解决：

- 应用层消息边界识别
- TCP 粘包
- TCP 半包
- 消息类型区分
- 消息体长度校验
- 协议版本校验
- 文本消息路由
- 在线客户端列表同步
- 文件传输消息描述

当前协议版本：

```text
Version = 1
```

截至 Stage 10，QtLink 应用层协议已经定义：

```text
文本发送请求
文本消息下发
在线客户端列表
文件传输开始请求
文件传输开始通知
文件数据块请求
文件数据块下发
文件传输结束请求
文件传输结束通知
```

其中：

```text
文本消息与在线客户端列表
→ 已接入 Client / Server 业务流程

文件传输协议
→ Stage 10 已完成协议定义和 Codec
→ Stage 11 开始接入真正的文件发送、接收和进度处理
```

---

# 2. Protocol Layer

QtLink 当前网络层次为：

```text
QtLink Application Protocol
        ↓
TCP
        ↓
IP
        ↓
Ethernet / Wi-Fi
```

QtLink 中定义的：

```text
Packet
MessageType
TextMessage
OnlineClientList
FileStart
FileChunk
FileEnd
```

全部属于：

```text
应用层协议
```

即使 `FileChunk` 使用二进制格式，它仍然属于应用层，而不是传输层。

真正的传输层协议仍然是：

```text
TCP
```

---

# 3. Packet Format

所有 QtLink 消息统一封装为：

```text
+-------------+---------+-------------+----------------+------------------+
| Magic       | Version | MessageType | BodyLength     | Body             |
+-------------+---------+-------------+----------------+------------------+
| 4 bytes     | 1 byte  | 2 bytes     | 4 bytes        | BodyLength bytes |
+-------------+---------+-------------+----------------+------------------+
```

固定 Header 大小：

```text
4 + 1 + 2 + 4 = 11 bytes
```

因此：

```cpp
HEADER_SIZE = 11
```

---

## 3.1 Magic

长度：

```text
4 bytes
```

固定值：

```text
QLNK
```

对应：

```text
'Q' 'L' 'N' 'K'
```

用于判断当前数据是否属于 QtLink 协议。

Magic 不正确时，该 Packet 被认为是非法协议数据。

---

## 3.2 Version

长度：

```text
1 byte
```

当前协议版本：

```text
1
```

对应：

```cpp
VERSION = 1
```

当前实现只接受：

```text
Version = 1
```

---

## 3.3 MessageType

长度：

```text
2 bytes
```

类型：

```cpp
quint16
```

当前定义：

| 数值 | MessageType        | 方向            | 说明                           |
| ---: | ------------------ | --------------- | ------------------------------ |
|    1 | `TextSendRequest`  | Client → Server | 请求向指定在线客户端发送文本   |
|    2 | `TextMessage`      | Server → Client | 向目标客户端下发文本           |
|    3 | `OnlineClientList` | Server → Client | 同步在线客户端列表             |
|    4 | `FileStartRequest` | Client → Server | 请求开始一次文件传输           |
|    5 | `FileStart`        | Server → Client | 通知接收方开始接收文件         |
|    6 | `FileChunkRequest` | Client → Server | 发送一个文件数据块             |
|    7 | `FileChunk`        | Server → Client | 向接收方下发文件数据块         |
|    8 | `FileEndRequest`   | Client → Server | 通知服务器文件数据已经发送完成 |
|    9 | `FileEnd`          | Server → Client | 通知接收方文件数据传输完成     |

接收到未定义的 MessageType 时：

```text
InvalidData
```

---

## 3.4 BodyLength

长度：

```text
4 bytes
```

类型：

```cpp
quint32
```

表示：

```text
Body 的字节长度
```

当前最大 Body 大小：

```text
8 MiB
```

即：

```cpp
MAX_BODY_SIZE = 8 * 1024 * 1024
```

如果：

```text
BodyLength > MAX_BODY_SIZE
```

则 Packet 非法。

---

## 3.5 Body

Body 的具体格式由：

```text
MessageType
```

决定。

当前分成两种。

控制消息和文本消息：

```text
UTF-8 JSON
```

包括：

```text
TextSendRequest
TextMessage
OnlineClientList
FileStartRequest
FileStart
FileEndRequest
FileEnd
```

文件数据块：

```text
Binary
```

包括：

```text
FileChunkRequest
FileChunk
```

---

# 4. Byte Order

QtLink Header 中所有多字节整数统一使用：

```text
Big Endian
```

即大端字节序。

包括：

```text
MessageType
BodyLength
```

FileChunk 中的：

```text
offset
```

同样使用：

```text
Big Endian
```

大端可以理解为：

```text
高有效字节先出现
```

例如：

```text
0x12345678
```

按大端编码为：

```text
12 34 56 78
```

---

# 5. TCP Stream Parsing

TCP 不保证：

```text
一次 write()
=
一次 readyRead()
=
一个完整 QtLink Packet
```

因此每一个 TCP 连接都必须维护独立的：

```cpp
QByteArray receiveBuffer;
```

接收过程：

```text
QTcpSocket::readAll()
        ↓
append 到 receiveBuffer
        ↓
PacketParser 尝试解析
```

---

## 5.1 Parser Flow

解析流程：

```text
检查 Header 是否完整
        ↓
校验 Magic
        ↓
校验 Version
        ↓
校验 MessageType
        ↓
读取 BodyLength
        ↓
校验 MAX_BODY_SIZE
        ↓
检查完整 Body 是否已经到达
        ↓
解析一个完整 Packet
```

如果：

```text
receiveBuffer.size()
<
HEADER_SIZE + BodyLength
```

说明当前发生：

```text
半包
```

必须保留现有 receiveBuffer，等待后续数据。

---

## 5.2 ParseResult

```cpp
enum class ParseResult
{
    Success,
    NeedMoreData,
    InvalidData
};
```

### Success

成功解析一个完整 Packet。

只移除：

```text
当前成功解析的 Packet
```

后续 Packet 数据继续保留。

### NeedMoreData

当前 TCP 字节不足。

必须：

```text
保持 receiveBuffer 不变
```

### InvalidData

数据违反 QtLink 协议。

例如：

```text
Magic错误
Version错误
MessageType非法
BodyLength超限
Packet解码失败
```

---

# 6. Text Message Protocol

## 6.1 TextSendRequest

方向：

```text
Client → Server
```

MessageType：

```text
1
```

JSON Body：

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

字段：

| 字段         | 类型    | 说明                  |
| ------------ | ------- | --------------------- |
| `receiverId` | integer | 目标客户端当前连接 ID |
| `content`    | string  | 文本内容              |

要求：

```text
receiverId > 0
receiverId <= quint32 最大值
receiverId 必须为整数
```

Client 不提交：

```text
senderId
```

---

## 6.2 TextMessage

方向：

```text
Server → Client
```

MessageType：

```text
2
```

JSON Body：

```json
{
  "senderId": 1,
  "content": "hello"
}
```

Server 根据当前连接 Session 得到真实：

```text
senderId
```

不能信任客户端自行声明发送者身份。

---

# 7. Online Client List

## 7.1 OnlineClientList

方向：

```text
Server → Client
```

MessageType：

```text
3
```

JSON Body：

```json
{
  "selfId": 1,
  "clientIds": [1, 2, 3]
}
```

字段：

| 字段        | 类型          | 说明                        |
| ----------- | ------------- | --------------------------- |
| `selfId`    | integer       | 当前客户端自己的临时连接 ID |
| `clientIds` | integer array | 当前可以通信的在线客户端 ID |

要求：

```text
selfId > 0
clientIds 中所有 ID > 0
selfId 必须存在于 clientIds
```

Client UI 根据：

```text
selfId
```

排除自己。

---

# 8. Connection Identity

当前 QtLink V1 没有：

```text
账号系统
登录系统
永久 userId
好友系统
```

Server 为每一次 TCP 连接分配：

```text
clientId
```

例如：

```text
Client A → 1
Client B → 2
Client C → 3
```

clientId 只在当前连接生命周期中有效。

断开并重新连接后：

```text
clientId 可能改变
```

因此：

```text
clientId
senderId
receiverId
selfId
```

都不能被理解为永久用户身份。

---

# 9. Directed Text Routing

假设：

```text
Client A = 1
Client B = 2
Client C = 3
```

A 向 B 发送：

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

Server 从 A 当前 Session 得到：

```text
senderId = 1
```

然后通过：

```cpp
m_sessions.find(receiverId)
```

查找：

```text
receiverId
→ ClientSession
→ QTcpSocket
```

再构造：

```json
{
  "senderId": 1,
  "content": "hello"
}
```

只发送给：

```text
Client B
```

Client C 不会收到。

---

# 10. Server Session Model

当前 Server 使用：

```cpp
QHash<quint32, ClientSession> m_sessions;
```

逻辑关系：

```text
clientId
   ↓
ClientSession
├── QTcpSocket*
└── receiveBuffer
```

每一个客户端拥有独立：

```cpp
QByteArray receiveBuffer;
```

避免不同 TCP 连接的数据互相混合。

---

# 11. File Transfer Identity

每一次文件传输拥有独立：

```text
transferId
```

它和：

```text
clientId
```

不是同一个概念。

区别：

```text
clientId
→ 标识当前 TCP 客户端连接

transferId
→ 标识一次具体文件传输
```

---

## 11.1 transferId Format

QtLink 使用：

```text
UUID
```

作为 transferId。

业务层统一表示为：

```text
550e8400-e29b-41d4-a716-446655440000
```

即：

```cpp
QUuid::WithoutBraces
```

格式。

以下形式：

```text
{550e8400-e29b-41d4-a716-446655440000}
```

如果是合法 UUID，可以在 Codec 中规范化为：

```text
550e8400-e29b-41d4-a716-446655440000
```

非法 UUID：

```text
abc
123
hello
```

必须拒绝。

---

# 12. FileStartRequest

方向：

```text
Client → Server
```

MessageType：

```text
4
```

作用：

通知 Server：

```text
我要开始一次文件传输
```

JSON Body：

```json
{
  "receiverId": 2,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

字段：

| 字段         | 类型         | 说明                  |
| ------------ | ------------ | --------------------- |
| `receiverId` | integer      | 文件目标客户端连接 ID |
| `transferId` | string(UUID) | 本次文件传输唯一标识  |
| `fileName`   | string       | 文件名                |
| `fileSize`   | integer      | 文件总字节数          |

要求：

```text
receiverId > 0

transferId
→ 必须是合法 UUID

fileName
→ 非空

fileSize
→ >= 0
```

允许：

```text
fileSize = 0
```

即零字节文件。

fileName 只表示：

```text
report.pdf
```

不应该包含发送方完整本地路径，例如：

```text
C:\Users\xxx\Documents\report.pdf
```

---

# 13. FileStart

方向：

```text
Server → Client
```

MessageType：

```text
5
```

Server 不直接转发 Client 的 FileStartRequest。

而是重新构造：

```json
{
  "senderId": 1,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

字段：

| 字段         | 类型         | 说明                      |
| ------------ | ------------ | ------------------------- |
| `senderId`   | integer      | 文件实际发送者当前连接 ID |
| `transferId` | string(UUID) | 本次文件传输唯一标识      |
| `fileName`   | string       | 文件名                    |
| `fileSize`   | integer      | 文件总字节数              |

senderId 必须由：

```text
Server 当前发送连接 Session
```

确定。

不能由发送方客户端自行声明。

---

# 14. FileChunk Protocol

文件数据块不使用 JSON。

原因是文件内容本身属于：

```text
任意二进制数据
```

如果放入 JSON，通常需要：

```text
Base64
```

等额外编码，会增加数据量和编解码成本。

因此：

```text
FileChunkRequest
FileChunk
```

统一采用二进制 Body。

---

## 14.1 FileChunkRequest

方向：

```text
Client → Server
```

MessageType：

```text
6
```

---

## 14.2 FileChunk

方向：

```text
Server → Client
```

MessageType：

```text
7
```

FileChunkRequest 和 FileChunk 使用相同 Body Schema：

```text
+----------------+----------------+----------------------+
| transferId     | offset         | chunkData            |
+----------------+----------------+----------------------+
| 16 bytes       | 8 bytes        | remaining bytes      |
+----------------+----------------+----------------------+
```

---

## 14.3 transferId Binary Format

transferId 使用 UUID 的：

```cpp
QUuid::toRfc4122()
```

表示。

固定长度：

```text
16 bytes
```

解码使用：

```cpp
QUuid::fromRfc4122()
```

然后统一转换为：

```cpp
QUuid::WithoutBraces
```

字符串形式。

---

## 14.4 offset

offset 类型：

```cpp
qint64
```

长度：

```text
8 bytes
```

使用：

```text
Big Endian
```

编码。

offset 表示：

> 当前 chunkData 在完整文件中的起始字节位置。

例如标准 Chunk 为 65536 bytes 时：

```text
Chunk 1:
offset = 0

Chunk 2:
offset = 65536

Chunk 3:
offset = 131072
```

要求：

```text
offset >= 0
```

负 offset 非法。

---

## 14.5 chunkData

固定元数据长度：

```text
16 + 8 = 24 bytes
```

因此：

```text
chunkData
=
Body 中第 24 字节之后的全部数据
```

不单独存：

```text
chunkLength
```

因为外层 Packet 已经存在：

```text
BodyLength
```

所以：

```text
chunkLength
=
BodyLength - 24
```

---

## 14.6 Empty Chunk

当前协议规定：

```text
FileChunk 必须包含实际文件数据
```

因此：

```text
Body.size() <= 24
```

视为非法 FileChunk。

零字节文件不发送 FileChunk。

零字节文件流程可以直接：

```text
FileStart
→ FileEnd
```

---

## 14.7 Binary Safety

chunkData 可以包含任意字节，包括：

```text
0x00
0x80
0xFF
```

不能按照：

```text
C字符串
UTF-8字符串
JSON字符串
```

解释。

它必须作为原始：

```cpp
QByteArray
```

处理。

---

# 15. FileEndRequest

方向：

```text
Client → Server
```

MessageType：

```text
8
```

JSON Body：

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

表示：

```text
发送方已经完成该 transferId 对应文件的数据发送
```

---

# 16. FileEnd

方向：

```text
Server → Client
```

MessageType：

```text
9
```

Body 与 FileEndRequest 相同：

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

因为两者 Body Schema 完全一致，所以：

```text
FileEndRequest
FileEnd
```

共用同一个：

```text
encodeEnd()
decodeEnd()
```

Codec。

MessageType 决定消息语义和方向，而 Codec 只负责 Body 格式。

---

# 17. File Transfer Message Flow

完整文件传输协议流程为：

```text
Client A
   │
   │ FileStartRequest
   ▼
 Server
   │
   │ FileStart
   ▼
Client B
```

然后：

```text
Client A
   │
   │ FileChunkRequest
   ▼
 Server
   │
   │ FileChunk
   ▼
Client B
```

上述 Chunk 过程重复多次。

最后：

```text
Client A
   │
   │ FileEndRequest
   ▼
 Server
   │
   │ FileEnd
   ▼
Client B
```

---

# 18. File Transfer Routing Principle

只有：

```text
FileStartRequest
```

需要显式携带：

```text
receiverId
```

后续：

```text
FileChunkRequest
FileEndRequest
```

只需要：

```text
transferId
```

Server 后续将根据 transferId 对应的文件传输状态确定：

```text
senderId
receiverId
fileSize
当前offset
```

因此不需要在每个 FileChunk 中重复携带 receiverId。

这可以避免：

```text
FileStart 指定 Client 2

后续某个 Chunk 又突然指定 Client 3
```

这种不一致情况。

---

# 19. File Chunk Size

Stage 11 计划采用的默认文件读取粒度：

```text
64 KiB
```

即：

```text
64 × 1024
=
65536 bytes
```

注意：

```text
64 KiB
```

只是：

```text
QtLink应用层文件分块大小
```

不是：

```text
TCP Segment大小
MTU
```

TCP 和底层网络仍会根据自身机制继续分段传输。

---

# 20. MAX_BODY_SIZE and FileChunk

当前：

```text
MAX_BODY_SIZE = 8 MiB
```

FileChunk Body 包含：

```text
16 bytes UUID
+
8 bytes offset
+
chunkData
```

因此正常的：

```text
64 KiB chunkData
```

远小于协议 Body 最大限制。

外层 PacketParser 仍然负责：

```text
BodyLength <= MAX_BODY_SIZE
```

校验。

---

# 21. Packet Encoding Relationship

文件 Chunk 的编码存在两个不同层级。

第一层：

```text
FileChunkCodec
```

负责：

```text
transferId
offset
chunkData
        ↓
[16-byte UUID][8-byte offset][chunkData]
        ↓
QByteArray Body
```

第二层：

```text
PacketCodec
```

负责：

```text
QtLink Header
+
FileChunk Body
        ↓
完整 QtLink Packet QByteArray
```

最终：

```cpp
QTcpSocket::write()
```

发送完整 Packet。

`PacketCodec` 不会重新解释或转换 FileChunk Body。

Body 通过：

```cpp
writeRawData(
    packet.body.constData(),
    packet.body.size());
```

按原始字节写入完整 Packet。

---

# 22. Protocol Validation Rules

QtLink 当前主要协议规则：

1. Magic 必须为：

```text
QLNK
```

2. Version 必须为：

```text
1
```

3. MessageType 必须属于当前定义范围。

4. 多字节协议整数使用 Big Endian。

5. BodyLength 不得超过：

```text
8 MiB
```

6. Parser 必须正确处理粘包。

7. Parser 必须正确处理半包。

8. NeedMoreData 时不得破坏 receiveBuffer。

9. Success 时只移除当前完整 Packet。

10. 每个 TCP 连接必须维护独立 receiveBuffer。

11. clientId 是临时连接 ID，不是永久 userId。

12. senderId 由 Server 根据连接 Session 确定。

13. receiverId 必须对应当前有效目标连接。

14. Client 与 Server 都不能把 senderId 当作客户端可信输入。

15. transferId 必须是合法 UUID。

16. transferId 在业务层统一使用 QUuid::WithoutBraces 格式。

17. FileStartRequest 携带 receiverId。

18. FileStart 携带 senderId。

19. FileChunkRequest / FileChunk 共用相同二进制 Body。

20. FileEndRequest / FileEnd 共用相同 JSON Body。

21. FileChunk offset 必须 >= 0。

22. FileChunk 必须包含非空 chunkData。

23. 零字节文件允许不产生任何 FileChunk。

24. 文件原始数据不得作为普通文本字符串解释。

25. Server 重新编码业务消息后仍必须保证输出 Body 满足 MAX_BODY_SIZE。

---

# 23. Current Automated Tests

协议测试当前覆盖：

```text
PacketCodec
PacketParser
TextMessageCodec
OnlineClientListCodec
FileTransferControlCodec
FileChunkCodec
MessageType validation
```

文件协议测试包括：

```text
FileStartRequest 正常往返
FileStart 正常往返
零字节文件
receiverId / senderId 边界
字段缺失
字段类型错误
合法 / 非法 UUID
UUID 格式规范化
FileEnd 正常及非法输入
FileChunk 二进制往返
0x00 / 0x80 / 0xFF 原始字节
非零 offset
短数据
缺失 payload
Null UUID
负 offset
decode失败后输出参数保持原值
```

当前完整协议测试集已经通过。

---

# 24. Stage 10 Boundary

截至 Stage 10：

```text
文件传输 MessageType
✅

FileStartRequest / FileStart Codec
✅

FileChunkRequest / FileChunk Codec
✅

FileEndRequest / FileEnd Codec
✅

UUID transferId 规则
✅

offset 二进制格式
✅

文件协议自动测试
✅
```

但以下功能尚未在 Stage 10 实现：

```text
文件选择
QFile读取
真正发送FileStart
真正循环发送FileChunk
接收端创建文件
接收端写盘
文件传输进度
发送背压控制
文件传输Session业务状态
```

这些属于：

```text
Stage 11
```

---

# 25. Future Protocol Extensions

后续预计继续增加：

```text
Heartbeat
Reconnect相关状态
可选文件完整性校验
可选断点续传
```

新增协议能力时继续遵循：

```text
统一 Packet Framing
+
明确 MessageType
+
明确 Body Schema
+
严格 Decode 校验
+
自动测试
```

不能绕过现有 PacketParser 在同一个 TCP Socket 上另外定义一套不兼容的流解析方式。