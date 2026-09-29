# QtLink Application Protocol

## 1. Protocol Overview

QtLink runs over a TCP byte stream. Every application message consists of a fixed-size Header followed by a variable-size Body:

```text
+------------+---------+-------------+------------+-------------------+
| Magic 4 B  | Ver 1 B | Type 2 B    | Length 4 B | Body Length bytes |
+------------+---------+-------------+------------+-------------------+
```

- Header size: `11` bytes
- Magic: ASCII `QLNK`
- Version: `1`
- MessageType: unsigned 16-bit integer
- BodyLength: unsigned 32-bit integer containing only the Body size
- Multi-byte integer byte order: BigEndian
- Maximum Body size: `8 * 1024 * 1024` bytes (8 MiB)

The current Header does not include compression, checksum, or encryption fields.

## 2. Packet Header

| Offset | Size | Field | Current value / type | Description |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | Magic | `QLNK` | Protocol identifier |
| 4 | 1 | Version | `1` | Protocol version |
| 5 | 2 | MessageType | `quint16` | Message type, BigEndian |
| 7 | 4 | BodyLength | `quint32` | Body length, BigEndian |
| 11 | BodyLength | Body | bytes | JSON, binary Chunk, or empty Body |

`PacketCodec` writes these fields during encoding. Decoding and parsing reject an invalid Magic, an unsupported Version, an undefined MessageType, or a BodyLength greater than `MAX_BODY_SIZE`.

## 3. Message Types and Directions

| Value | MessageType | Direction | Body |
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
| 14 | `HeartbeatRequest` | Client → Server | Empty |
| 15 | `HeartbeatResponse` | Server → Client | Empty |

`isValidMessageType()` only checks whether the numeric value belongs to the current protocol. The Client and Server dispatch switches additionally enforce direction: the Server does not accept Server-to-Client message types, and the Client does not accept Client-to-Server message types.

## 4. JSON Body Rules

JSON messages use UTF-8 Compact JSON. Decoding requires the root value to be an object and validates required fields, field types, and numeric ranges. The examples below are formatted for readability; transmitted JSON contains no extra whitespace.

### 4.1 TextSendRequest

A Client requests delivery of text to a selected online Client:

```json
{
  "receiverId": 2,
  "content": "hello"
}
```

- `receiverId`: an integer greater than 0 and no greater than the maximum `quint32` value
- `content`: a string

The Client does not provide `senderId`. The Server derives sender identity from the current TCP Session.

### 4.2 TextMessage

The Server delivers text to the receiving Client:

```json
{
  "senderId": 1,
  "content": "hello"
}
```

- `senderId`: an integer greater than 0 and no greater than the maximum `quint32` value
- `content`: a string

### 4.3 OnlineClientList

The Server generates an online list separately for each Client:

```json
{
  "selfId": 1,
  "clientIds": [1, 2, 3]
}
```

- `selfId`: the temporary `clientId` of the receiving connection
- `clientIds`: an array of current online IDs, each a valid non-zero `quint32`
- `selfId` must be present in `clientIds`

## 5. File Transfer Control Messages

A `transferId` must be a valid, non-Null UUID. The codecs normalize it to the lowercase `QUuid::WithoutBraces` form, for example:

```text
550e8400-e29b-41d4-a716-446655440000
```

### 5.1 FileStartRequest

Client → Server:

```json
{
  "receiverId": 2,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

- `receiverId`: a valid non-zero `quint32`
- `transferId`: a valid UUID
- `fileName`: a non-empty string
- `fileSize`: an integer greater than or equal to 0; zero-byte files are valid

### 5.2 FileStart

Server → Client:

```json
{
  "senderId": 1,
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "fileName": "report.pdf",
  "fileSize": 10485760
}
```

The field rules match `FileStartRequest`, except that the Server removes the validated destination `receiverId` and inserts the trusted `senderId` derived from the sending Session.

### 5.3 FileEndRequest / FileEnd

The messages have different directions but share the same Body:

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

`FileEndRequest` means the Sender has submitted all Chunks within the declared file range. After verifying `nextOffset == fileSize`, the Server forwards `FileEnd` to the Receiver and enters the phase that waits for the Receiver's final acknowledgement.

### 5.4 FileCompleteRequest / FileComplete

Both messages reuse the transferId-only Body:

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000"
}
```

The Receiver sends `FileCompleteRequest` after validating the file size and successfully flushing and closing the file. The Server verifies that the requester is the transfer's Receiver and that the phase is valid, sends `FileComplete` to the Sender, and then removes the ActiveTransfer.

### 5.5 FileAbortRequest / FileAbort

Both messages reuse the same Body:

```json
{
  "transferId": "550e8400-e29b-41d4-a716-446655440000",
  "reason": 2
}
```

`reason` maps to `FileAbortReason`:

| Value | Name | Meaning |
| ---: | --- | --- |
| 1 | `PeerDisconnected` | A peer connection was lost |
| 2 | `IoError` | Local file I/O failed |
| 3 | `Cancelled` | Cancellation reason; the current UI has no manual cancel action |
| 4 | `ProtocolError` | Transfer state or data violated the protocol |
| 5 | `Timeout` | The transfer made no progress for the timeout interval |

The Server accepts a `FileAbortRequest` only from that transfer's Sender or Receiver. Abort, Complete, and disconnect converge according to the first terminal event that the Server validates and processes.

## 6. FileChunk Binary Body

`FileChunkRequest` and `FileChunk` have different directions but use the same Body layout:

```text
+----------------------+------------------+----------------------+
| UUID RFC 4122 16 B   | Offset qint64 8 B| ChunkData 1..65536 B|
+----------------------+------------------+----------------------+
```

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 16 | transferId | The 16-byte result of `QUuid::toRfc4122()`; a Null UUID is invalid |
| 16 | 8 | offset | Signed `qint64`, BigEndian, must be greater than or equal to 0 |
| 24 | 1..65536 | chunkData | Raw binary bytes; may contain `0x00` and arbitrary non-text data |

The Body must contain at least 25 bytes. A 24-byte metadata-only Body with no Chunk data is invalid. One Chunk carries at most 64 KiB. The Server also verifies that offset exactly matches the ActiveTransfer's `nextOffset` and that the Chunk does not extend beyond the declared `fileSize`.

A zero-byte file sends no FileChunk and follows the Start → End → Complete path directly.

## 7. Heartbeat

`HeartbeatRequest` and `HeartbeatResponse` require a strictly empty Body:

```text
BodyLength = 0
```

The Client periodically sends `HeartbeatRequest`, and the Server replies with `HeartbeatResponse`. Any complete valid Packet can refresh the corresponding connection activity timestamp. Heartbeat detects stale connections when no business data is flowing and carries no business fields.

## 8. TCP Packet Coalescing and Fragmentation

TCP provides an ordered byte stream but does not preserve application message boundaries. QtLink keeps a persistent receive buffer in the Client and in every Server `ClientSession`:

1. `readyRead` appends the result of `readAll()` to the current connection's buffer.
2. If the buffer contains fewer than 11 bytes, parsing returns `NeedMoreData`.
3. Once the Header is complete, the parser validates Magic, Version, MessageType, and BodyLength.
4. If the complete Packet has not arrived, all bytes remain in the buffer and parsing returns `NeedMoreData`.
5. When a complete Packet is available, the parser outputs that Packet and removes only its bytes from the front of the buffer.
6. The caller continues parsing any following Packets in the same buffer.

The same logic therefore supports:

- one complete Packet;
- a Header or Body split across multiple reads;
- multiple Packets delivered in one read;
- one complete Packet followed by a partial next Packet.

An invalid Header is treated as a connection-level protocol error. The current implementation terminates that connection instead of scanning arbitrary byte positions to resynchronize on Magic.
