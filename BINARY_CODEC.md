# Binary codec — `toBytes` / `fromBytes` (C17)

The reopened `bytes.from(v)` option (README `## Bytes`, "stays an option…"):
a reflection-driven whole-object binary codec, defined under C12's shape —
read-only over `const *O` views, `T!` with **value-level** failures, and
the same instantiation constraint as the JSON codec (scalars, string, enum,
array, struct — no reachable `*T`, `any`, or disposable fields). One shape,
two encodings: `toJson`/`fromJson` (text) and `toBytes`/`fromBytes` (this
sketch). Same shape means no cycles and no pointer arms in either, and a
parser is constructible from the shape alone.

## The codec

```c
// -- emit failures are values, not types: a non-finite float, or the depth
// -- cap. Parse failures carry the frame offset (parser cursor) where the
// -- frame stopped making sense. Both mirror the JSON kinds — and both are
// -- spelled by bare `return kind { … }` (auto-wrap, C19).
BinaryWriteError error = { message string; cause error }
BinaryParseError error = { message string; offset uint; cause error }

// toBytes walks obj through const views only — reflection never takes,
// moves, mutates, or disposes (the toJson precedent, C12). The output is a
// fresh bytes buffer in the caller's statement-block arena: dies with the
// caller's block, no dispose, `bytes` is arena memory. The wire format is
// pinned **Endian.big** — set on the buffer before any write, so the frame
// is portable by construction, not by the caller's platform.
toBytes func [O] (obj const *O, n := 0) bytes! = {
  if n > 64 then
    return BinaryWriteError { message = "too deeply nested" }
  b bytes
  b.endian(Endian.big)
  match O {
    String(s)   => { b.writeU32(s.length); b.writeStr(s) }
    Integer(i)  => b.writeI64(i)
    Float(f)    => if f.isFinite() then b.writeF64(f)
                   else return BinaryWriteError { message = "non-finite float" }
    Boolean(x)  => b.writeU8(if x then 0x01 else 0x00)
    Enum(e)     => { b.writeU32(e.name.length); b.writeStr(e.name)     // arm name, note 7
                     b += toBytes(e.value(), n + 1)! }                  // deep failures propagate
    Array(a)    => { b.writeU32(a.length)
                     loop e in a { b += toBytes(e, n + 1)! } }
    Struct(s)   => { loop f in s.fields { b += toBytes(f.value(obj), n + 1)! } }
  }
  return b                        // auto-wrap: success
}
```

The wire format falls out of the shape — the table is normative:

| shape | frame |
|---|---|
| `Integer(i)` | `writeI64(i)` — 8 bytes |
| `Float(f)` | `writeF64(f)` — 8 bytes; non-finite → `BinaryWriteError` |
| `Boolean(x)` | 1 byte `0x01` / `0x00` |
| `String(s)` | `writeU32(len)` + UTF-8 body |
| `Enum(e)` | `writeU32(len)` + arm name + payload frame |
| `Array(a)` | `writeU32(count)` + element frames |
| `Struct(s)` | field frames, declaration order — no length, no separators |

## Parsing

```c
// parse: genuinely fallible — a truncated or malformed frame is data
// (`T!`), never an abort. Success &-creates the whole O graph in the
// caller's statement-block arena (C7) and returns a view: survives the call,
// bulk-freed at the caller's block exit, no dispose (the newList precedent).
// fromBytes pins the frame's endianness before parsing: `copy()` owns the
// input in the caller's arena, `endian(Endian.big)` makes reads agree with
// the format toBytes wrote.
fromBytes func [O] (b const bytes) *O! = {
  frame := b.copy()               // owned arena copy, reads can't touch the caller's buffer
  frame.endian(Endian.big)
  parser := BinaryParser { input = &frame, at = 0 }     // BinaryParser: intrinsic
  try obj := parser.parse[O]()                          // nested &-creates land in
  return obj                                            // the caller's arena (C7)
  catch e
  return BinaryParseError { message = "binary parse failed", offset = parser.at, cause = e }
}
```

The BinaryParser's one discipline, normative for any coded parser:

> **Every intrinsic read is preceded by a bounds check.** The bytes intrinsics
> (`as*`, `peek`) **panic** past the end — the README calls that a programmer
> bug, and that is true *outside* parsing. Inside parsing a short frame is
> **data**: a truncated length prefix or body must become `BinaryParseError
> { offset = p.at }` *before* any intrinsic read is allowed to fire, so the
> parser never turns malformed input into an abort. Concretely, each arm does
> `need(n)` → intrinsic read → advance the cursor:
>
> ```c
> need func (p *BytesParser, n uint) uint! = {
>   if p.at + n > p.input.length then
>     return BinaryParseError { message = "truncated frame", offset = p.at,
>                               cause = Error { message = "%d{n} bytes needed" } }
>   return p.at               // the read offset — become available
> }
> ```
>
> A string arm then proves the length prefix, reads it *absolutely* (the
> checked offset is the cursor-free `as*At` position), proves the body, and
> copies it out:
>
> ```c
> // (inside the String arm of parse[O])
> off := p.need(4)!                      // prove the length prefix is in the frame
> len := p.input.asU32At(off)            // read it — bounds proven
> p.need(len)!                           // prove the body is too
> s := p.input[off + 4 ..< off + 4 + len].asStr(len)   // slice view → owned copy
> p.at = off + 4 + len
> return s                   // auto-wrap: success
> ```

## Notes

1. **C12 shape, binary encoding.** The JSON-shaped constraint carries over
   unchanged (scalars, string, enum, array, struct; no `*T` / `any` /
   disposable) — the same structural reasons hold: no reachable references
   means the serializer cannot cycle, and the parser can construct the graph
   from the shape alone. `toBytes` accepts exactly the types `toJson` does;
   the constraint is one shape, not two.

2. **Failures are values on both sides.** Emit: non-finite float, depth cap →
   `BinaryWriteError`. Parse: any short or self-inconsistent frame →
   `BinaryParseError { offset }` — the offset is the parser cursor when the
   check failed, the same role `parser.at` plays in `JsonParseError`. No
   panic is reachable from malformed data, and no `T!` is ever swallowed —
   the `!` propagations in `toBytes` are bare (no guarded scope in the
   reflection arms), so a deep failure surfaces as the whole call's failure.

3. **Endianness is the codec's, not the platform's.** `toBytes` pins
   `Endian.big` on its working buffer before any write — frames are portable
   by construction. `fromBytes` copies the input into the caller's arena and
   pins the same endianness before parsing, so reads agree with the frame
   both ends of the wire wrote. (The intrinsic reads use absolute,
   cursor-free forms after a check — see `need` — so the parser never disturbs
   a caller-owned buffer's cursor.)

4. **`need`-first, read-second — the coded-parser rule.** `as*` panics past
   the end by design; that is a defense against *programmer* bugs. A server
   parsing hostile input must never let that panic fire: the parser converts
   shortness to `BinaryParseError` first (note 2). This is the one way the
   codec adopts the I/O section's rule — EOF / short reads are data — inside
   reflection.

5. **The arena does the bookkeeping.** `toBytes` output is an arena `bytes`
   buffer: no `dispose`, freed with the caller's block. `fromBytes`
   &-creates the `O` graph in the caller's statement-block arena (C7) and
   returns `*O`; strings inside it are owned copies of the frame's slices
   (the `.asStr(len)` at the end of the string arm). Nothing escapes the
   caller's block, nothing leaks.

6. **Enum arms are length-prefixed names, not ordinal tags.** `Enum(e)`
   writes the arm's *name* (as JSON's `%q{e.name}` does), so `fromBytes`
   needs no arm-ordinal table and a reordering of arms does not change the
   wire. The compaction trade (a name vs a one-byte ordinal) is documented,
   and an ordinal-tagged variant is a per-protocol choice for hot paths —
   manual codecs (README `## Bytes`) remain the norm when the wire is
   performance-sensitive or externally specified.

7. **Struct evolution is append-only.** Fields are positional, in declaration
   order, with no length or separators (the shape self-describes). Appending
   a field at the *end* keeps old frames readable by the new type; reordering
   fields or inserting in the middle changes the wire. The JSON codec's
   `#json.name`-class metaprogramming does not exist for binary — a
   positional format cannot carry renames.

8. **vs Go.** `toBytes`/`fromBytes` sit where Go puts `encoding/binary` plus
   a bespoke reflection walk: the wire is `binary.Write` fixed-width with an
   explicit length-prefix convention, and `json.Decoder`-style arm-by-arm
   construction is the `encoding/json` reflection. The differences are the
   ownership ones: the output buffer and the parsed graph are arena memory in
   the caller's block (no GC, no `close`), failures pass through `T!` with
   an offset, and panics stay unreachable from data.