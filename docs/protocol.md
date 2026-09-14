# QtLink Protocol

## 1. Packet Format

QtLink 数据包由固定长度的 Header 和可变长度的 Body 组成。

```text
Packet = Header + Body
```

Header 包含以下字段：

### 1.1 Magic Number

- 内容：`QLNK`
- 大小：4 bytes
- 用于识别 QtLink 数据包并进行基本合法性检查

### 1.2 Version

- 类型：`quint8`
- 大小：1 byte
- 当前协议版本：`1`
- 用于标识 QtLink 协议版本

### 1.3 Message Type

- 类型：`quint16`
- 大小：2 bytes
- 用于表示当前 Packet 的消息类型

当前支持：

| 数值 | 消息类型          | 方向            |
| ---: | ----------------- | --------------- |
|    1 | `TextSendRequest` | Client → Server |
|    2 | `TextMessage`     | Server → Client |

### 1.4 Body Length

- 类型：`quint32`
- 大小：4 bytes
- 表示 Body 的实际字节长度
- 当前 QtLink 实现限制单个 Body 最大为 8 MiB

### 1.5 Body

- 实际消息内容
- Body 的具体格式由 `Message Type` 决定
- 当前文本消息使用 JSON 格式

---

## 2. Header Layout

| Field        | Type               |    Size |
| ------------ | ------------------ | ------: |
| Magic Number | 4-byte fixed value | 4 bytes |
| Version      | `quint8`           |  1 byte |
| Message Type | `quint16`          | 2 bytes |
| Body Length  | `quint32`          | 4 bytes |

固定 Header 大小：

```text
4 + 1 + 2 + 4 = 11 bytes
```

因此：

```text
HEADER_SIZE = 11
```

---

## 3. Byte Order

所有多字节整数字段统一使用 Big Endian（大端字节序，也称网络字节序）。

包括：

- `Message Type`
- `Body Length`

例如：

```text
quint16 value = 0x1234
```

在网络中的字节顺序为：

```text
12 34
```

---

## 4. Message Types

### 4.1 TextSendRequest

方向：

```text
Client → Server
```

作用：

客户端请求服务器发送一条文本消息。

JSON Body：

```json
{
  "content": "hello"
}
```

字段说明：

| 字段      | 类型   | 说明                     |
| --------- | ------ | ------------------------ |
| `content` | string | 客户端希望发送的文本内容 |

客户端发送的 `TextSendRequest` 不包含 `senderId`。

服务器根据当前 TCP 连接对应的 `ClientSession` 确定真实发送者身份，不信任客户端自行声明发送者 ID。

---

### 4.2 TextMessage

方向：

```text
Server → Client
```

作用：

服务器将文本消息转发给其他客户端，并携带真实发送者的连接 ID。

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
| `senderId` | integer | 服务器为发送客户端连接分配的临时 ID |
| `content`  | string  | 文本消息内容                        |

`senderId` 由服务器根据发送请求对应的 `ClientSession` 写入。

客户端不能自行指定 `senderId`。

---

## 5. Text Message Flow

假设 Client A 对应：

```text
clientId = 1
```

Client A 输入：

```text
hello
```

### Step 1：Client A 构造 TextSendRequest

```json
{
  "content": "hello"
}
```

对应：

```text
MessageType = TextSendRequest
```

然后通过 QtLink Packet 发送给 Server。

---

### Step 2：Server 解析请求

Server 根据收到数据的：

```text
QTcpSocket*
```

查找：

```text
m_sessions
```

得到对应的：

```text
ClientSession
```

从中取得：

```text
clientId = 1
```

---

### Step 3：Server 构造 TextMessage

Server 不直接转发 Client 原始 Packet，而是重新构造：

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

然后发送给其他在线客户端。

---

### Step 4：其他 Client 接收消息

其他客户端解析 `TextMessage`，得到：

```text
senderId = 1
content = hello
```

UI 显示：

```text
Client 1: hello
```

---

## 6. Protocol Rules

当前 QtLink V1 遵循以下规则：

1. TCP 是字节流协议，不假设一次 `write()` 对应一次 `read()`。
2. 所有接收端必须维护持久化接收缓冲区。
3. Packet 必须根据固定 Header 和 `Body Length` 完成分帧。
4. 不完整 Packet 保留在接收缓冲区中等待后续数据。
5. 成功解析一个 Packet 后，只移除该 Packet 对应的字节。
6. 单个 Body 当前最大允许 8 MiB。
7. Client → Server 的文本消息使用 `TextSendRequest`。
8. Server → Client 的文本消息使用 `TextMessage`。
9. `senderId` 由 Server 根据连接会话确定。
10. 非法协议数据应被拒绝，必要时断开对应连接。