# QtLink 应用层协议

## 1. 协议概览

QtLink 运行在 TCP 字节流之上。每条应用层消息由固定长度 Header 和变长 Body 组成：

```text
+------------+---------+-------------+------------+------------------+
| Magic 4 B  | Ver 1 B | Type 2 B    | Length 4 B | Body Length bytes|
+------------+---------+-------------+------------+------------------+
```

- Header 总长度：`11` 字节
- Magic：ASCII `QLNK`
- Version：`1`
- MessageType：无符号 16 位整数
- BodyLength：无符号 32 位整数，只表示 Body 字节数
- 多字节整数：BigEndian
- 最大 Body：`8 * 1024 * 1024` 字节（8 MiB）

Header 当前不包含压缩、校验和或加密字段。

## 2. Packet Header

| Offset | 长度 | 字段 | 当前值 / 类型 | 说明 |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | Magic | `QLNK` | 协议标识 |
| 4 | 1 | Version | `1` | 协议版本 |
| 5 | 2 | MessageType | `quint16` | 消息类型，BigEndian |
| 7 | 4 | BodyLength | `quint32` | Body 长度，BigEndian |
| 11 | BodyLength | Body | bytes | JSON、二进制 Chunk 或空 Body |

`PacketCodec` 编码时写入上述字段。解码和解析会拒绝错误 Magic、错误 Version、未定义 MessageType，以及超过 `MAX_BODY_SIZE` 的 BodyLength。

## 3. MessageType 与方向

| 数值 | MessageType | 方向 | Body |
| ---: | --- | --- | --- |
| 1 | `TextSendRequest` | Client → Server | JSON |
| 2 | `TextMessage` | Server → Client | JSON |
| 3 | `OnlineClientList` | Server → Client | JSON |
| 4 | `FileStartRequest` | Client → Server | JSON |
| 5 | `FileStart` | Server → Client | JSON |
| 6 | `FileChunkRequest` | Client → Server | Binary |
| 7 | `FileChunk` | Server → Client | Binary |
| 8 | `FileEndRequest` | Client → Server | JSON |
| 9 | `FileEnd` | Server → Client | JSON |
| 10 | `FileAbortRequest` | Client → Server | JSON |
| 11 | `FileAbort` | Server → Client | JSON |
| 12 | `FileCompleteRequest` | Client → Server | JSON |
| 13 | `FileComplete` | Server → Client | JSON |
| 14 | `HeartbeatRequest` | Client → Server | 空 |
| 15 | `HeartbeatResponse` | Server → Client | 空 |

`isValidMessageType()` 只判断数值是否属于当前协议。Client 和 Server 的分发 switch 还会分别检查消息方向：Server 不接受 Server → Client 类型，Client 不接受 Client → Server 类型。

## 4. JSON Body 约定

JSON 消息使用 UTF-8 Compact JSON。解码要求根节点为 object，并校验必需字段、字段类型和数值范围。下面为了阅读采用格式化展示；实际发送内容不带多余空白。

### 4.1 TextSendRequest

Client 请求向指定在线客户端发送文本：

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

- `receiverId`：大于 0 且不超过 `quint32` 最大值的整数
- `content`：字符串

Client 不提供 `senderId`。Server 从当前 TCP Session 获得发送者身份。

### 4.2 TextMessage

Server 向接收 Client 下发文本：

```json
{
  "senderId": 1,
  "content": "hello"
}
```

- `senderId`：大于 0 且不超过 `quint32` 最大值的整数
- `content`：字符串

### 4.3 OnlineClientList

Server 为每个 Client 分别生成在线列表：

```json
{
  "selfId": 1,
  "clientIds": [1, 2, 3]
}
```

- `selfId`：该接收连接自己的临时 `clientId`
- `clientIds`：当前在线 `clientId` 数组，元素均为合法非零 `quint32`
- `selfId` 必须出现在 `clientIds` 中

## 5. 文件传输控制消息

`transferId` 必须是合法、非 Null UUID。Codec 会统一规范化为 `QUuid::WithoutBraces` 的小写字符串形式，例如：

```text
550e8400-e29b-41d4-a716-446655440000
```

### 5.1 FileStartRequest

Client → Server：

```json
{
  "receiverId": 2,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

- `receiverId`：合法非零 `quint32`
- `transferId`：合法 UUID
- `fileName`：非空字符串
- `fileSize`：大于或等于 0 的整数，允许零字节文件

### 5.2 FileStart

Server → Client：

```json
{
  "senderId": 1,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

字段规则与 `FileStartRequest` 相同，但目标 `receiverId` 被 Server 根据 Session 验证后移除，改为由 Server 写入可信的 `senderId`。

### 5.3 FileEndRequest / FileEnd

两种消息方向不同，Body 相同：

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

`FileEndRequest` 表示 Sender 已提交声明范围内的全部 Chunk。Server 验证 `nextOffset == fileSize` 后向 Receiver 发送 `FileEnd`，并进入等待 Receiver 最终确认的阶段。

### 5.4 FileCompleteRequest / FileComplete

两种消息复用 transferId-only Body：

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

Receiver 在文件大小验证、flush 和 close 成功后发送 `FileCompleteRequest`。Server 验证请求者确为该 transfer 的 Receiver 且阶段正确，再向 Sender 发送 `FileComplete` 并删除 ActiveTransfer。

### 5.5 FileAbortRequest / FileAbort

两种消息复用相同 Body：

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "reason": 2
}
```

`reason` 对应 `FileAbortReason`：

| 数值 | 名称 | 含义 |
| ---: | --- | --- |
| 1 | `PeerDisconnected` | 对端连接断开 |
| 2 | `IoError` | 本地文件 I/O 失败 |
| 3 | `Cancelled` | 传输取消原因；当前 UI 未提供主动取消入口 |
| 4 | `ProtocolError` | 文件传输状态或数据违反协议 |
| 5 | `Timeout` | 文件传输长期无进展 |

Server 只允许 transfer 的 Sender 或 Receiver 提交 `FileAbortRequest`。Abort、Complete 和 disconnect 按 Server 首个验证成功并处理的终态事件收敛。

## 6. FileChunk 二进制 Body

`FileChunkRequest` 和 `FileChunk` 方向不同，Body 布局相同：

```text
+----------------------+------------------+----------------------+
| UUID RFC 4122 16 B   | Offset qint64 8 B| ChunkData 1..65536 B|
+----------------------+------------------+----------------------+
```

| Offset | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 16 | transferId | `QUuid::toRfc4122()` 的 16 字节表示，Null UUID 非法 |
| 16 | 8 | offset | 有符号 `qint64`，BigEndian，必须大于或等于 0 |
| 24 | 1..65536 | chunkData | 原始二进制数据，可包含 `0x00` 和任意非文本字节 |

Body 至少为 25 字节；只有 24 字节元数据而没有 Chunk 数据时解码失败。单个 Chunk 最大为 64 KiB。Server 还会验证 offset 必须严格等于 ActiveTransfer 的 `nextOffset`，并且 Chunk 不能超过声明的 `fileSize`。

零字节文件不发送 FileChunk，直接走 Start → End → Complete 流程。

## 7. Heartbeat

`HeartbeatRequest` 和 `HeartbeatResponse` 的 Body 必须严格为空：

```text
BodyLength = 0
```

Client 定时发送 `HeartbeatRequest`，Server 回复 `HeartbeatResponse`。任意完整合法 Packet 都可以刷新相应连接的活动时间；Heartbeat 用于在没有业务数据时检测失活连接，不承载业务字段。

## 8. TCP 粘包与半包处理

TCP 只提供有序字节流，不保留应用消息边界。QtLink 在 Client 和每个 Server `ClientSession` 中分别维护 persistent receive buffer：

1. `readyRead` 将 `readAll()` 结果追加到当前连接的 buffer；
2. buffer 少于 11 字节时返回 `NeedMoreData`；
3. Header 完整后先校验 Magic、Version、MessageType 和 BodyLength；
4. 完整 Packet 尚未到达时保留全部数据并返回 `NeedMoreData`；
5. 完整 Packet 到达后输出当前 Packet，只从 buffer 前端移除该 Packet；
6. 调用方继续循环解析 buffer 中的后续 Packet。

因此同一逻辑同时支持：

- 单个完整 Packet；
- Header 或 Body 分多次到达；
- 多个 Packet 一次到达；
- 一个完整 Packet 后紧跟下一 Packet 的部分数据。

解析出非法 Header 时，当前实现把它视为 connection-level protocol error 并终止该连接，而不是尝试在任意字节位置重新同步 Magic。
