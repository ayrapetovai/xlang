# Bytes: the design idea

*Draft design capture — rev 2: `bytes` as a builtin ByteBuffer. Not yet codified
into README.md; forks are marked and open.*

*Rev 1 (scalar `byte` + `[]byte` + fixed-width ints) is superseded: rev 2 dissolves
the fixed-width-types question — widths live in the buffer API, not the type system.*

---

## The model

`bytes` is a builtin mutable buffer: byte data plus a cursor, all functions built in
(intrinsics). There is no `uint16` type — `asU16()` reads 2 bytes and returns a
`uint`. Reads and writes advance the cursor; writes auto-grow the buffer.

```c
b bytes = {1, 2, 3, 4}   // buffer, cursor at 0
i uint = b.asU16()       // reads 2 bytes at the cursor → uint; cursor += 2
b.reset()                // cursor = 0
b.writeI16(i)            // writes 2 bytes at the cursor; grows if needed
b.writeU8(127)
```

## Construction and strings

```c
b bytes                         // empty buffer
b bytes = {1, 2, 3, 4}
s := b.string()                 // utf-8 string of the contents
b2 := bytes.from("Hello")       // the from-machinery, reverse direction
b += more                       // append at the end (like string +)
```

## Reads — advance the cursor

| intrinsic | reads | returns |
|---|---|---|
| `asU8()` | 1 byte | `uint` (or scalar `byte`, see fork) |
| `asU16()` | 2 bytes | `uint` |
| `asU32()` | 4 bytes | `uint` |
| `asU64()` | 8 bytes | `uint` |
| `asI8/16/32/64()` | 1/2/4/8 bytes | `int` |
| `asF32()/asF64()` | 4/8 bytes | `float` |
| `asStr(n)` | n bytes | `string` |

## Writes — advance the cursor, auto-grow

`writeU8/16/32/64`, `writeI8/16/32/64`, `writeF32/64`, `writeStr(s)`. A value is
truncated to its width (wrap semantics). Writing past the current length grows the
buffer; writing within it overwrites in place.

## Cursor and meta

`reset()` → cursor 0 · `pos(n)` absolute · `skip(n)` · `remaining()` ·
`eof()` (remaining == 0) · `length`.

## Transforms — produce a new buffer

```c
orred := b.or(0xFFFF)
anded := b.and(0x0F)
xored := b.xor(key)      // key bytes, element-wise
flip  := b.not()
```

## Slicing — views for framing

`b[2..<5]` stays a view that writes through (fork, from rev 1); owned copy via
`.copy()`. Framing pattern: read the length prefix, slice the body, parse the slice.

## Socket + files

- `socket.recv(fd)` — one byte
- `socket.recv(fd, n)` — at most n bytes, `Result[bytes]`
- `socket.recvExact(fd, n)` — exactly n, or `Error`
- `socket.send(fd, data bytes)` — `Result[uint]`
- `File struct = { fd Fd }` — disposable on the handle barrier; `dispose` = `file.close`

## Struct codecs

With a cursor, manual codecs are trivial; canonical `bytes.from(v)` becomes optional.

```c
toBytes func (r *Request, b bytes) = {
  b.writeU16(r.kind)
  b.writeU16(r.count)
}
fromBytes func (b *bytes) Request = {
  Request {
    kind  = b.asU16()
    count = b.asU16()
  }
}

readRequest func (conn &Connection) Result[Request] = {
  hdr  := socket.recvExact(conn.fd, 4)   // uint32 length prefix
  n    := hdr.asU32()
  body := socket.recvExact(conn.fd, n)
  fromBytes(&body)
}

sendRequest func (conn *Connection, req Request) Result[uint] = {
  body bytes
  body.writeU16(req.kind)
  body.writeU16(req.count)
  frame bytes
  frame.writeU32(body.length)
  frame += body
  socket.send(conn.fd, frame)
}

readLine func (conn *Connection) Result[string] = {
  line bytes
  loop {
    match socket.recv(conn.fd) {
      Ok(b) =>
        if b == 0x0A then return Ok(line.string())
        line.writeU8(b)
      Error(e) =>
        if line.length > 0 then return Ok(line.string())
        return Error("connection closed: %s{e}")
    }
  }
}
```

## Decision set (recommendations marked)

- **Endianness** — default big (network byte order); per-buffer switch
  `b.endian(Endian.little)`.
- **Fork 1 — overflow** — byte arithmetic wraps modulo 256; add `0x` / `0b` literals.
- **Fork 3 — bounds** — `as*` / `write*` past the end is a **panic** (programmer
  bug; `recvExact` already guarantees lengths at the I/O boundary), not `Result`.
- **Fork 4 — slicing** — `b[i..<j]` is a write-through view; `.copy()` for owned.
- **Fork 6 — transforms** — `or`/`and` per-byte with `mask & 0xFF` (recommended),
  per-word, or big-integer; `xor(key bytes)` and `not()` element-wise.
- **Fork 7 — scalar `byte`** — keep an 8-bit Copyable scalar (single values,
  `char`/`byte` adjacency) or drop it; `asU8()` return type follows.
- **Naming** — `read()`/`write()` as byte-level synonyms, or `asU8`/`writeU8` only.
- **String interop** — `b.string()` / `bytes.from(s)` are binary-safe.
- **Codecs** — manual `toBytes`/`fromBytes` is the recommended shape; canonical
  `bytes.from(v)` stays optional.

**Open questions:**

- Canonical `bytes.from(v)`: keep as a reflection-driven option, or drop in favor of
  manual codecs?
- `socket.recv(fd)` returns `Result[byte]` or `Result[uint]` (depends on Fork 7)?
- Width of `char` in any future canonical encoding.