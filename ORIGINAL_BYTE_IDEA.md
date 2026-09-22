# Bytes: the design idea

*Draft design capture — how `byte`, `[]byte`, and struct↔bytes serialization could work.
Not yet codified into README.md; forks are marked and open.*

`byte` already exists in the spec as a scalar — "Copyable types: scalars (int, float,
`char`, `byte`, bool)" — but it is a ghost: no operators, no literals, no `[]byte`
story, no serialization. This document fills that gap in four layers.

---

## 1. The scalar

`byte` is a raw unsigned 8-bit value, 0..255. It is **not** `char`: `char` is text (a
code point, `'\n'`, `'A'`), `byte` is data. No implicit mixing — casts are explicit,
per the language's rule.

```c
b byte = 0xA5          // hex literals — the spec needs 0x / 0b added
n := byte.from(-1)     // wraps: 0xFF — same wrap semantics as uint.from(-1) = 1
c := char.from(65)     // 'A'
b = byte.from('A')     // 0x41 (wraps for code points above 255)

b = 255 + 1            // 0 — 8-bit arithmetic wraps, like uint8
b = b << 1             // high bit falls off
b2 := b <<~ 3          // cyclic rotate — the language already has <<~ and >>~
mask := b & 0x0F       // & | ~ ^ all apply
```

**Fork 1 — overflow: wrap or trap?**
Wrap (recommended). It matches the existing `uint.from(-1) → 1` precedent, and
protocol work (checksums, counters) would panic-spam under trapping. Rule: *byte
arithmetic is modulo 256*.

---

## 2. `[]byte`

The workhorse. Construction, indexing, `.length`, and ranges exist already; the
additions:

```c
buf []byte = { 0x48, 0x65, 0x6C, 0x6C, 0x6F }   // {H, e, l, l, o}
buf.push(0x00)                    // append one byte
buf += more                       // concat, like string +
chunk := buf[2..<5]               // slice — see Fork 4
```

**String interop** — strings are length-known binary-safe carriers, so the roundtrip
is exact:

```c
data := "Hello".bytes()    // []byte, utf-8 bytes, a copy
text := string.from(data)  // exact roundtrip, binary-safe
```

**Fork 4 — slicing: view or copy?**
`buf[2..<5]` as a **view** (recommended) that writes through
(`buf[2..<5][0] = 0x0A` mutates the source) — matches the view philosophy and enables
parse-in-place framing. An owned copy is explicit: `buf[2..<5].copy()`. A view
escaping its source dies by the existing "returning a view of a local" rule.

**Fork 2 — multi-byte for real (the key decision).** "2 bytes, 4 bytes" needs
fixed-width integers; today there are only `int`/`uint`/`float` with no width
defined. Two ways:

- *Add `int8 int16 int32 int64 uint8 uint16 uint32 uint64`* (and fix `int`/`uint`
  = 64-bit). Then protocol structs carry real fields (`length uint32`,
  `type uint16`) and `bytes.from` is deterministic.
- *Only `byte` + hand-assembly* (`b[0] | b[1] << 8 | …`) — every field hand-packed.

Recommendation: the first, with `byte` staying **distinct** from `uint8` (so `[]byte`
keeps its special string/socket functions and `[]uint8` is just numbers). With fixed
widths, endian-correct chunk ops are explicit:

```c
u32 := uint32.from(buf[0..<4], Endian.big)     // 2 bytes / 4 bytes / 8 bytes
buf[4..<8].writeFrom(x, Endian.little)
```

---

## 3. Struct ↔ bytes

`bytes.from(v)` / `T.from(bytes)` — reusing the *first-arg-is-the-type* `from`
machinery (`int.from("1234")`), generated at compile time from the type descriptor:

```c
// canonical encoding, defined once, from reflection — not raw memory:
//   scalars   → fixed width, big-endian (network byte order)
//   string    → uint64 length + utf-8 bytes
//   []T       → uint64 count + elements in order
//   struct    → fields in declaration order, packed, no padding
//   enum      → its tag as int
//   *T views  → pointee encoded (there are no null pointers)
payload := bytes.from(msg)           // total, []byte
msg2    := Message.from(payload)     // Result[Message]

// framing: slice first, then parse
frame := Request.from(body)          // body is buf[0..<n]; buf advances by slicing
```

It composes with the socket layer:

```c
readFrame func (conn &Connection) Result[Request] = {
  hdr  := socket.recvExact(conn.fd, 8)         // uint64 length prefix
  n    := uint64.from(hdr, Endian.big)
  body := socket.recvExact(conn.fd, n)
  Request.from(body)
}
```

**Fork 5 — canonical vs raw view.**
A raw `asBytes(&msg)` memory view is rejected (recommended): struct padding may be
uninitialized, so sending it could leak memory contents, and the layout is not
portable. Canonical-from-descriptor is total, deterministic, and the reflection
machinery makes it cheap. Deserialization returns `Result` ("bad data is data, not a
panic"). Cyclic structures (possible in arenas!) become an error via an
address-visited set, not a hang.

---

## 4. Socket + files

```c
socket.recv(fd)        // one byte, Result[byte]   (as today, for text delimiting)
socket.recv(fd, n)     // at most n bytes, Result[[]byte]
socket.recvExact(fd,n) // exactly n, or Error   ← the useful one
socket.send(fd, data)  // now takes []byte too (const string overload stays)

File struct = { fd Fd }                  // same handle barrier as Connection
dispose func (f &File) = { file.close(f.fd) }
file.read(f, n) / file.write(f, data)
```

Files slot straight into the existing `Disposable`/handle-barrier model — "a file
handle" is already named as a resource in `## Resources`.

---

## Decision set (all recommended)

- **Fork 1** — byte arithmetic wraps modulo 256; add `0x` / `0b` literals.
- **Fork 2** — add fixed-width ints (`int8`..`uint64`); `int`/`uint` = 64-bit;
  `byte` stays distinct from `uint8`.
- **Fork 4** — slices are views with explicit `.copy()`.
- **String interop** — `string.bytes()` / `string.from()` are binary-safe copies.
- **Fork 5** — canonical big-endian descriptor encoding, `Result` on deserialize,
  no raw memory views.
- **I/O** — `recv` / `recvExact` / `send` bulk byte API; disposable `File`.

**Open questions not yet settled:**

- Endianness default for canonical struct encoding (big = network byte order is the
  current default; per-field toggling is a possible future nicety).
- Whether `recv(fd)` returns `Result[byte]` or stays text-oriented `Result[char]`
  for `readLine`.
- Width of `char` in canonical encoding (fixed 4-byte code point vs 1 byte).