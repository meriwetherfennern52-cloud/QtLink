# QtLink Protocol Specification

## 1. Overview

QtLink 使用 TCP 进行客户端与服务器之间的通信。

由于 TCP 是面向字节流的传输协议，本身不保留应用层消息边界，因此 QtLink 在 TCP 之上定义自己的应用层数据包格式，用于解决：

- 消息边界识别
- TCP 粘包
- TCP 半包
- 消息类型区分
- 消息体长度校验
- 协议版本校验

当前协议版本：

```text
Version = 1
```

当前协议主要支持：

- 文本消息发送请求
- 文本消息定向转发
- 在线客户端列表同步

文件传输、心跳等协议将在后续版本中扩展。

---

# 2. Packet Format

一个完整的 QtLink Packet 由：

```text
+-------------+---------+-------------+----------------+------------------+
| Magic       | Version | MessageType | BodyLength     | Body             |
+-------------+---------+-------------+----------------+------------------+
| 4 bytes     | 1 byte  | 2 bytes     | 4 bytes        | BodyLength bytes |
+-------------+---------+-------------+----------------+------------------+
```

组成。

固定 Header 大小：

```text
4 + 1 + 2 + 4 = 11 bytes
```

因此：

```cpp
HEADER_SIZE = 11
```

---

## 2.1 Magic

长度：

```text
4 bytes
```

固定值：

```text
QLNK
```

对应字节：

```text
'Q' 'L' 'N' 'K'
```

作用：

用于判断接收到的数据是否属于 QtLink 协议。

如果 Magic 不正确，则当前数据包被认为是非法协议数据。

---

## 2.2 Version

长度：

```text
1 byte
```

当前值：

```text
1
```

对应：

```cpp
VERSION = 1
```

作用：

用于标识 QtLink 协议版本。

当前实现只接受 Version 1。

---

## 2.3 MessageType

长度：

```text
2 bytes
```

类型：

```cpp
quint16
```

用于标识当前 Packet 的业务消息类型。

当前定义：

| 数值 | MessageType        | 方向            | 说明                               |
| ---: | ------------------ | --------------- | ---------------------------------- |
|    1 | `TextSendRequest`  | Client → Server | 客户端请求向指定在线客户端发送文本 |
|    2 | `TextMessage`      | Server → Client | 服务器向目标客户端下发文本消息     |
|    3 | `OnlineClientList` | Server → Client | 服务器同步当前在线客户端连接列表   |

接收到未定义的 MessageType 时，数据包应被认为是非法数据。

---

## 2.4 BodyLength

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
Body 的字节数
```

例如：

```text
BodyLength = 100
```

表示 Header 后还需要读取：

```text
100 bytes
```

才能得到完整 Packet。

当前协议最大 Body 大小：

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

则该 Packet 被认为是非法数据。

---

## 2.5 Body

长度：

```text
BodyLength bytes
```

当前 Stage 9 中的业务消息 Body 使用：

```text
UTF-8 JSON
```

进行编码。

不同 MessageType 对应不同 JSON 结构。

---

# 3. Byte Order

QtLink Header 中所有多字节整数统一使用：

```text
Big Endian
```

即网络字节序。

包括：

```text
MessageType
BodyLength
```

Client 和 Server 必须使用相同字节序进行编码和解码。

---

# 4. TCP Stream Parsing

TCP 是字节流协议，不保证：

```text
一次 write()
=
一次 readyRead()
=
一个完整 Packet
```

因此 QtLink 不允许直接把一次 `readAll()` 当作一个完整应用层消息。

每个 TCP 连接都必须维护独立的：

```cpp
QByteArray receiveBuffer;
```

接收到新数据后：

```text
socket->readAll()
        ↓
append 到 receiveBuffer
        ↓
PacketParser 尝试解析
```

---

## 4.1 Parser 基本流程

解析一个 Packet 时：

```text
1. 检查是否至少具有 11 字节 Header
        ↓
2. 校验 Magic
        ↓
3. 校验 Version
        ↓
4. 校验 MessageType
        ↓
5. 读取 BodyLength
        ↓
6. 校验 BodyLength <= MAX_BODY_SIZE
        ↓
7. 检查缓冲区是否包含完整 Body
        ↓
8. 取出一个完整 Packet
```

如果：

```text
receiveBuffer.size()
<
HEADER_SIZE + BodyLength
```

说明发生半包。

此时必须等待后续 TCP 数据，不能提前移除缓冲区中的已有内容。

---

## 4.2 ParseResult

当前 Parser 使用：

```cpp
enum class ParseResult
{
    Success,
    NeedMoreData,
    InvalidData
};
```

语义如下。

### Success

表示成功解析一个完整 Packet。

此时：

```text
只移除当前成功解析的数据包
```

如果 receiveBuffer 中还有后续 Packet，应继续解析。

---

### NeedMoreData

表示当前数据不足以组成完整 Packet。

典型情况：

```text
Header 不完整
```

或：

```text
Header 已完整，但 Body 尚未全部到达
```

此时：

```text
receiveBuffer 必须保持不变
```

等待下一次 `readyRead()`。

---

### InvalidData

表示当前数据违反 QtLink 协议。

例如：

- Magic 错误
- Version 错误
- MessageType 非法
- BodyLength 超过最大限制
- Packet 解码失败

Parser 返回 InvalidData 时，不应把非法数据当作正常 Packet 继续处理。

具体是否断开连接，由调用方根据当前错误策略决定。

---

# 5. Message Body Format

## 5.1 TextSendRequest

方向：

```text
Client → Server
```

MessageType：

```text
1
```

作用：

客户端请求服务器将一条文本消息发送给指定的当前在线客户端。

JSON Body：

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

字段说明：

| 字段         | 类型    | 说明                                |
| ------------ | ------- | ----------------------------------- |
| `receiverId` | integer | 目标客户端当前连接的临时 `clientId` |
| `content`    | string  | 需要发送的文本内容                  |

约束：

```text
receiverId > 0
receiverId <= quint32 最大值
```

`receiverId` 必须是整数，不能是：

- 字符串
- 小数
- 负数
- 0
- 超过 `quint32` 范围的数字

客户端不提交：

```text
senderId
```

发送者身份由 Server 根据当前 TCP 连接对应的 Session 确定。

---

## 5.2 TextMessage

方向：

```text
Server → Client
```

MessageType：

```text
2
```

作用：

服务器将一条文本消息定向发送给目标客户端。

JSON Body：

```json
{
  "senderId": 1,
  "content": "hello"
}
```

字段说明：

| 字段       | 类型    | 说明                                |
| ---------- | ------- | ----------------------------------- |
| `senderId` | integer | 实际发送者当前连接的临时 `clientId` |
| `content`  | string  | 文本消息内容                        |

`senderId` 由服务器根据发送请求所在连接确定。

服务器不能直接信任客户端自行声明发送者身份。

---

## 5.3 OnlineClientList

方向：

```text
Server → Client
```

MessageType：

```text
3
```

作用：

服务器向客户端同步当前可以进行通信的在线连接列表，并告知当前客户端自己的临时连接 ID。

JSON Body：

```json
{
  "selfId": 1,
  "clientIds": [1, 2, 3]
}
```

字段说明：

| 字段        | 类型          | 说明                                   |
| ----------- | ------------- | -------------------------------------- |
| `selfId`    | integer       | 当前接收客户端自身的临时连接 ID        |
| `clientIds` | integer array | 当前处于可通信状态的客户端连接 ID 列表 |

约束：

```text
selfId > 0
```

所有：

```text
clientIds[i] > 0
```

并且：

```text
selfId
```

必须存在于：

```text
clientIds
```

中。

例如：

```json
{
  "selfId": 2,
  "clientIds": [1, 2, 3]
}
```

合法。

而：

```json
{
  "selfId": 2,
  "clientIds": [1, 3]
}
```

不符合当前协议语义。

---

# 6. Connection Identity

当前 QtLink V1 没有：

- 用户注册
- 用户登录
- 永久账号 ID
- 好友系统
- 永久联系人身份

服务器为每个新 TCP 连接分配一个：

```text
clientId
```

例如：

```text
Client A → clientId = 1
Client B → clientId = 2
Client C → clientId = 3
```

该 ID 仅表示：

```text
当前服务器运行期间的当前 TCP 连接
```

它不是永久用户身份。

---

## 6.1 selfId

`selfId` 表示：

```text
当前客户端自己的 clientId
```

用于客户端识别：

```text
在线列表中哪一个 ID 是自己
```

客户端 UI 不应把自己列为消息接收者。

---

## 6.2 senderId

`senderId` 表示：

```text
文本消息实际发送者的 clientId
```

Server 必须从：

```text
当前处理请求对应的 ClientSession
```

中获取 senderId。

不能使用客户端提交的 senderId。

---

## 6.3 receiverId

`receiverId` 表示：

```text
当前文本消息的目标 clientId
```

Client 可以选择 receiverId。

Server 必须再次验证：

- receiverId 是否存在
- receiverId 是否仍处于可通信状态
- receiverId 是否等于发送者自己的 clientId

如果：

```text
receiverId == senderId
```

Server 当前实现拒绝该次发送。

---

## 6.4 Reconnection

当前 `clientId` 是临时连接 ID。

因此：

```text
Client 2
```

断线以后重新连接时，不保证仍然得到：

```text
clientId = 2
```

例如可能变为：

```text
clientId = 5
```

当前协议不把 clientId 当作永久用户身份保存。

---

# 7. Online Client List Synchronization

服务器在以下事件发生后重新广播在线客户端列表：

```text
新客户端连接
客户端断开
```

Server 根据当前有效连接生成：

```text
clientIds
```

并分别为每个客户端构造自己的：

```text
selfId
```

例如当前：

```text
Client 1
Client 2
Client 3
```

在线。

发送给 Client 1：

```json
{
  "selfId": 1,
  "clientIds": [1, 2, 3]
}
```

发送给 Client 2：

```json
{
  "selfId": 2,
  "clientIds": [1, 2, 3]
}
```

发送给 Client 3：

```json
{
  "selfId": 3,
  "clientIds": [1, 2, 3]
}
```

因此同一次在线状态更新中：

```text
clientIds
```

基本相同，而：

```text
selfId
```

因接收客户端不同而不同。

---

## 7.1 Client UI Rules

客户端收到 OnlineClientList 后：

```text
1. 排除 selfId
2. 将其他 clientId 显示为可选接收者
```

例如：

```text
selfId = 1
clientIds = [1, 2, 3]
```

UI 可选择：

```text
Client 2
Client 3
```

客户端使用 QComboBox 的：

```text
显示文本
+
userData
```

分别保存：

```text
"Client 3"
+
真实 receiverId = 3
```

发送消息时必须读取实际保存的 receiverId，而不能通过显示字符串重新解析 ID。

---

## 7.2 Receiver Selection Preservation

在线列表发生变化时，客户端不能自动将用户已经选中的接收者切换成另一个客户端。

例如：

```text
Client 1 当前选择 Client 3

Client 4 上线
```

更新列表后，如果 Client 3 仍在线：

```text
仍然保持选择 Client 3
```

如果 Client 3 已经离线：

```text
清除当前选择
禁止发送
等待用户重新选择接收者
```

不能自动切换到：

```text
Client 2
```

避免消息被发送给错误的目标。

---

# 8. Directed Text Message Flow

假设当前：

```text
Client A：clientId = 1
Client B：clientId = 2
Client C：clientId = 3
```

Client A 希望向 Client B 发送：

```text
hello
```

---

## Step 1. Client A selects receiver

Client A 从在线客户端列表中选择：

```text
Client 2
```

得到：

```text
receiverId = 2
```

---

## Step 2. Client A creates TextSendRequest

Body：

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

然后封装：

```text
MessageType = TextSendRequest
```

再由 PacketCodec 编码成完整 QtLink Packet。

---

## Step 3. Server parses request

Server 从发送该 Packet 的 Session 得到：

```text
clientId = 1
```

因此：

```text
senderId = 1
```

客户端不需要，也不允许决定自己的 senderId。

---

## Step 4. Server validates receiver

Server 检查：

```text
receiverId != senderId
```

并通过 Session 表：

```text
receiverId
        ↓
ClientSession
        ↓
QTcpSocket
```

查找目标连接。

当前 Server 使用：

```cpp
m_sessions.find(receiverId)
```

直接查找目标 Session。

如果：

```text
receiverId
```

不存在，或对应 socket 已不处于可通信状态：

```text
拒绝此次转发
```

---

## Step 5. Server creates TextMessage

Server 不直接把客户端的原始 TextSendRequest 转发给目标客户端。

而是重新构造：

```json
{
  "senderId": 1,
  "content": "hello"
}
```

对应：

```text
MessageType = TextMessage
```

服务器重新编码 Body 后，必须再次检查：

```text
Body size <= MAX_BODY_SIZE
```

因为：

```text
输入 Body
```

和：

```text
输出 Body
```

不是同一份字节数据。

---

## Step 6. Server sends only to target

Server 只向：

```text
receiverId = 2
```

对应的 socket 写入数据。

因此：

```text
Client B
```

收到消息。

而：

```text
Client C
```

不会收到这条消息。

---

## Step 7. Client B displays message

Client B 解码得到：

```text
senderId = 1
content = hello
```

UI 可显示：

```text
Client 1: hello
```

---

# 9. Server Session Model

当前 Server 为每一个在线连接维护独立 Session。

逻辑结构：

```text
clientId
   ↓
ClientSession
├── QTcpSocket*
└── receiveBuffer
```

对应：

```cpp
QHash<quint32, ClientSession> m_sessions;
```

使用 `clientId` 作为 Hash Key，便于：

```text
receiverId
→ 直接查找目标连接
```

---

## 9.1 Independent Receive Buffer

每一个连接必须拥有独立：

```cpp
QByteArray receiveBuffer;
```

不能让多个客户端共享同一个 TCP 接收缓冲区。

例如：

```text
Client 1
→ receiveBuffer 1

Client 2
→ receiveBuffer 2

Client 3
→ receiveBuffer 3
```

否则不同 TCP 连接的数据可能被错误混合。

---

## 9.2 Connection Removal

客户端断开后，Server 应：

```text
1. 从 m_sessions 删除对应 clientId
2. 更新在线人数
3. 广播新的 OnlineClientList
4. 对 socket 调用 deleteLater()
```

删除 Session 后，其他客户端不应继续看到该 clientId。

---

# 10. Server Send Rules

Server 向 Socket 写入数据时使用：

```cpp
QTcpSocket::write()
```

`write()` 成功返回非负值，表示：

```text
数据已经成功提交给 Qt Socket 的发送缓冲机制
```

它不表示：

```text
对端已经成功收到消息
```

如果：

```text
write() < 0
```

Server 应记录对应 socket 的：

```cpp
errorString()
```

当前 Stage 9 不实现：

- 消息送达确认
- 已读状态
- 消息 ACK
- 自动重发

这些属于更高层的应用协议能力。

---

# 11. Protocol Validation Rules

当前 QtLink V1 遵循以下规则：

1. Packet Magic 必须为：

```text
QLNK
```

2. Version 必须为：

```text
1
```

3. MessageType 必须是当前协议已定义类型。

4. Header 中多字节整数统一使用 Big Endian。

5. BodyLength 不得超过：

```text
8 MiB
```

6. Parser 必须支持 TCP 粘包。

7. Parser 必须支持 TCP 半包。

8. `NeedMoreData` 时不得破坏 receiveBuffer。

9. `Success` 时只移除当前完整 Packet。

10. 不同 TCP 连接必须维护独立 receiveBuffer。

11. `clientId` 是当前 TCP 连接的临时 ID，不是永久用户 ID。

12. Client 通过 `receiverId` 指定当前在线消息接收目标。

13. Server 不接受 Client 自行声明 `senderId`。

14. senderId 必须来自当前发送连接对应的 Server Session。

15. 定向文本消息只发送给 receiverId 对应的目标连接。

16. Client UI 不允许选择自己作为 receiver。

17. Server 必须再次检查 receiverId 是否等于 senderId。

18. receiverId 不存在或目标 Socket 不可通信时，Server 必须安全拒绝本次转发。

19. OnlineClientList 中 selfId 必须属于 clientIds。

20. OnlineClientList 只应发布当前可以通信的连接。

21. 在线列表更新不能自动将用户选择切换到另一个 receiver。

22. Server 对业务消息重新编码后，必须再次检查输出 Body 是否超过 MAX_BODY_SIZE。

23. Server 发送失败时应记录对应 Socket 错误信息。

---

# 12. Current Protocol Examples

## 12.1 Client 2 sends text to Client 5

Client → Server：

```json
{
  "receiverId": 5,
  "content": "hello"
}
```

MessageType：

```text
TextSendRequest = 1
```

假设该请求实际上来自：

```text
clientId = 2
```

Server → Client 5：

```json
{
  "senderId": 2,
  "content": "hello"
}
```

MessageType：

```text
TextMessage = 2
```

---

## 12.2 Server sends online list to Client 2

当前在线：

```text
1
2
5
```

Server → Client 2：

```json
{
  "selfId": 2,
  "clientIds": [1, 2, 5]
}
```

MessageType：

```text
OnlineClientList = 3
```

Client 2 的接收者列表应显示：

```text
Client 1
Client 5
```

---

# 13. Current Non-goals

当前协议暂不负责：

- 用户注册
- 用户登录
- 永久用户 ID
- 好友关系
- 群聊
- TLS 加密
- 消息 ACK
- 已读回执
- 离线消息同步
- 跨服务器路由
- 文件传输
- 文件断点续传
- 心跳
- 自动重连

这些能力需要额外的应用层协议设计。

其中：

```text
文件传输
心跳
自动重连
```

会在 QtLink 后续阶段逐步加入。

---

# 14. Current Automated Protocol Tests

当前协议层自动测试覆盖：

### PacketCodec

- Packet 编码与解码
- Header 字段正确性
- Body 往返

### PacketParser

- 完整 Packet
- 半包
- 粘包
- 一个完整 Packet + 下一个半包
- 非法 Magic
- 非法 Version
- 非法 MessageType
- BodyLength 超限

### TextMessageCodec

- TextSendRequest 正常往返
- receiverId 正常解析
- 中文内容
- emoji
- JSON 转义字符
- 非法 JSON
- 缺失字段
- 字段类型错误
- receiverId 为 0
- receiverId 为负数
- receiverId 为小数
- receiverId 超过 quint32 最大值

### OnlineClientListCodec

- 正常 encode / decode
- 非法 JSON
- 缺失 selfId
- 缺失 clientIds
- 字段类型错误
- clientIds 中非法元素
- clientId 为 0
- clientId 为负数
- clientId 为小数
- clientId 超过 quint32 最大值
- selfId 不在 clientIds 中

当前 Stage 9 协议测试结果：

```text
41 passed
0 failed
0 skipped
```

其中包含 Qt Test 的初始化和清理流程。

---

# 15. Future Extension

后续协议预计继续增加新的 MessageType，例如：

```text
FileStart
FileChunk
FileEnd
Heartbeat
```

新增协议消息时应继续遵循：

```text
Packet Header
+
MessageType
+
明确的 Body Schema
+
严格输入校验
+
自动测试
```

的现有设计，不应绕过统一 Packet framing 直接在 TCP Socket 上定义另一套无边界数据格式。