# Hash Map

A generic open-addressed hash map. Written only with what the language
already has: `&`-created containers in the arena, `T?` as the honest
"maybe", the `+=` move-append family, `infix_operator==` for key equality,
and — the one mechanism this sketch establishes (ruling C16) — a **`hash`
function resolved per instantiation**, exactly like the ordering operators:
`hash func (k const *K) uint`.

There are **no null pointers**, so an empty bucket cannot be a `nil` pointer
and (worse) cannot be a *vacated* slot — a vacated slot is an unreadable,
tracked non-value (`OWNERSHIP_RULES.md` §6), the wrong thing for a
long-lived table. The sketch's answer: **open addressing over
`Slot { entry Entry[K, V]? }`** — an empty slot is the `T?` default
absence (`{}`), a real value (C19). Every slot is always some value; only
`count` says how many are live.

## Shape

```c
Entry struct [K, V] = {
  key K
  val V
}

// an empty slot is a real value — the `T?` default absence (`{}`) — never a
// null and never a vacated slot; the table holds only full/absent values
Slot struct [K, V] = {
  entry Entry[K, V]?
}

HashMap struct [K, V] = {
  buckets []Slot[K, V]   // omitted in a literal: declaration default — empty
  count    uint
}

Probe struct = {
  pos   uint
  found bool
}
```

## Hashing

`hash` is resolved per instantiation, like `infix_operator<` / `==` (C13):
built-ins exist for the primitive keys (`int`/`uint`/`byte`/`bool` — a
bijective multiplier; `string` — hashes its bytes); a user key type must
bring its own `hash` into scope. The one contract the checker cannot verify:

> **Congruence.** Keys equal under `infix_operator==` must hash equal.

Distribution quality is a runtime concern; congruence is the correctness
contract — an equal pair hashing into different buckets would make `get`
fail to find what `put` stored.

```c
Point struct = {
  x int
  y int
}

infix_operator== func (a const *Point, b const *Point) bool = {
  a.x == b.x && a.y == b.y
}

hash func (k const *Point) uint = {
  uint(k.x) * 31 + uint(k.y)
}
```

## Construction and queries

```c
newHashMap func [K, V] () *HashMap[K, V] = {
  m := &HashMap[K, V] {
    count = 0                // buckets omitted: default empty; grow() sizes it
  }
  m.grow()                   // 16 slots up front
  return m
}

// forward distance (b - a) mod len — uint-safe: a, b < len, so b + len - a < 2·len
dist func (a uint, b uint, len uint) uint = {
  if b >= a then
    return b - a
  return b + len - a
}

// the probe row: `key` present (found), else the first free slot. Reads go
// through a const view, so the unwrap *inspects* — nothing moves (note 3).
// A table under the load contract can never be full — `TableFullError` is a
// typed backstop: the failure is reported, never silently assumed away.
TableFullError error = { message string }

probe func [K, V] (m const *HashMap[K, V], key const *K) Probe! = {
  pos := hash(key) % m.buckets.length
  scanned uint = 0
  loop scanned < m.buckets.length {
    if kv := (&m.buckets[pos].entry)? then {   // const view — inspect, no move
      if kv.key == key then
        return Probe { pos = pos, found = true }   // auto-wrap: success
    } else
      return Probe { pos = pos, found = false }    // auto-wrap: success
    pos = (pos + 1) % m.buckets.length
    scanned += 1
  }
  return TableFullError { message = "hash table is full" }   // contract broken
}

// first free slot for `key` — terminates because the table is never full
probeEmpty func [K, V] (b const *[]Slot[K, V], key const *K) uint = {
  pos := hash(key) % b.length
  loop {
    if b[pos].entry == {} then      // absence test — legal on `T?`
      return pos
    pos = (pos + 1) % b.length
  }
}

// a view into the stored value — read-only by construction, invalidated by
// growth (note 4). Absence is data: `T?`, never `T!`; probe's backstop
// failure (a broken load contract) also reads as absence from here.
get func [K, V] (m const *HashMap[K, V], key const *K) const *V? = {
  if m.buckets.length == 0 then
    return                    // absence — before any probe
  try p := m.probe(key)       // Probe! — failure settles at the catch
  if !p.found then
    return                    // absence
  if kv := (&m.buckets[p.pos].entry)? then    // const map: inspection, no consumption
    return &kv.val            // auto-wrap: const *V into the map's slot
  return                      // slot absent — unreachable from a found probe
  catch _
  return                        // table corrupt — "not found" from get
}
```

## Mutation

```c
// move the pair out of a live slot — or absence, if the slot is free
// (put/remove call it only after a found probe, so absence never fires; the
// slot falls back to the `T?` default — absent — a real value, never
// uninitialized, and the `Entry[K, V]?` box carries the moved-out pair).
takeEntry func [K, V] (m *HashMap[K, V], pos uint) Entry[K, V]? = {
  return m.buckets[pos].entry?          // unwrap-propagation: absent slot → absence
}

put func [K, V] (m *HashMap[K, V], key K, val V) = {
  if m.buckets.length == 0 then
    m.grow()                          // belt and braces; newHashMap already sized
  try p := m.probe(key)       // Probe! — failure settles at the catch
  if p.found then {
    old := m.takeEntry(p.pos)         // move out — the slot is now absent (a value)
    m.buckets[p.pos].entry = Entry { key = key, val = val }   // re-wrap by assignment
    // `old` is deallocated at this block's exit — built-in machinery only:
    // K and V may not define `dispose` (note 7)
  } else {
    if m.count * 4 >= m.buckets.length * 3 then {   // load ≈ 0.75 → grow first
      m.grow()
    }
    pos := m.probeEmpty(key)
    m.buckets[pos].entry = Entry { key = key, val = val }  // over an absent slot
    m.count += 1
  }
  catch _
  return                        // load contract broken — the insert is skipped
}

// grow: a fresh table of twice the size, rehashed by moves. The old table is
// consumed slot-by-slot and dropped all-absent at the assignment (notes 4, 5).
grow func [K, V] (m *HashMap[K, V]) = {
  newLen := m.buckets.length * 2
  if newLen < 16 then
    newLen = 16
  nb []Slot[K, V] = {}
  loop i := 0; i < newLen {
    nb += Slot { entry = {} }         // move-append: every slot starts absent
    i += 1
  }
  loop i := 0; i < m.buckets.length {
    if kv := m.buckets[i].entry? then {   // owned slot: the unwrap moves it out
      pos := nb.probeEmpty(kv.key)    // nb is fresh — no conflicts, no replace
      nb[pos].entry = kv              // move-in; assignment wraps
    }
    i += 1
  }
  m.buckets = nb          // drops the old table — every entry absent by now
}

// backward-shift deletion: pull each following entry left that may (its home
// is not strictly between hole and r), until the first free slot; the
// trailing cleared slot is absent again. Every surviving key stays reachable
// from its home (note 6).
remove func [K, V] (m *HashMap[K, V], key const *K) V? = {
  if m.buckets.length == 0 then
    return
  try p := m.probe(key)       // Probe! — failure settles at the catch
  if !p.found then
    return
  if e := m.takeEntry(p.pos)? then {   // move out — p.pos is now absent
    len := m.buckets.length
    hole := p.pos
    r := (p.pos + 1) % len
    loop r != p.pos {
      if kv := (&m.buckets[r].entry)? then {   // inspect — decide without moving
        dh := dist(hole, hash(kv.key) % len, len)
        dr := dist(hole, r, len)
        if dh == 0 || dh > dr then {    // home not in (hole, r] — may shift left
          if shifted := m.takeEntry(r)? then
            m.buckets[hole].entry = shifted   // absence unreachable — inspected present
          hole = r
        }
      } else
        break                            // cluster ends
      r = (r + 1) % len
    }
    m.buckets[hole].entry = {}          // trailing slot cleared — absent again
    m.count -= 1
    return e.val                        // the key dies with e — dropped at return
  }
  return                        // absence unreachable — p.found said live
  catch _
  return                        // table corrupt — "not removed" from remove
}
```

## Usage

`"alice"` auto-borrows into the `const *K` key parameters, exactly like
operator operands (README, values auto-borrow into `const *T`).

```c
m *HashMap[string, int] = newHashMap()
m.put("alice", 30)
m.put("bob", 40)
m.put("alice", 31)                       // replace: the old pair is deallocated

if v := m.get("alice")? then
  out.println("alice is %d{v}")          // 31 — v: const *int, auto-deref
else
  out.println("alice missing")

removed int = m.remove("bob") ?? 0       // 40 — `??` falls back on absence
if old := m.remove("alice")? then
  out.println("alice held %d{old}")      // 31
else
  out.println("alice was gone")

total uint = 0
loop i := 0; i < m.buckets.length {      // const iteration — the documented form
  if kv := (&m.buckets[i].entry)? then   // view inspection — nothing moves
    total += uint(kv.val)
  i += 1
}
out.println("total %d{total}")  // 0 — both keys were removed
```

Custom keys need only `==` and `hash` in scope:

```c
m2 *HashMap[Point, string] = newHashMap()
m2.put(Point { x = 1, y = 2 }, "one-two")
m2.put(Point { x = 3, y = 4 }, "three-four")

if s := m2.get(Point { x = 1, y = 2 })? then
  out.println("%s{s}")        // "one-two" — s: const *string
else
  out.println("missing")
```

## Notes

1. **`hash` in scope (C16).** `hash func (k const *K) uint` is resolved by
   the compiler per instantiation — the same lookup path `infix_operator<`
   and `infix_operator==` already use (README Generics: "a template function
   that needs some function looks up the scope"; §11). Built-ins cover the
   primitive keys; user types declare their own. The **congruence contract**
   is the one thing the checker cannot see: equal keys must hash equal —
   document it beside every user `hash`, as the totality contract of C13 is
   documented beside every ordering.

2. **Empty slots are the default absence, never vacated.** `Entry[K, V]?`
   defaults to absent — a real value (`{}`), read by the `== {}` absence
   test, written by `= {}` (C19; §6). A vacated slot is a tracked non-value;
   reading it is a compile error until reinitialized (§6) — so vacated-state
   storage could never back a long-lived table. The `T?` default *is* the
   table's emptiness: no slot is ever uninitialized. This is the same
   reasoning that keeps the language free of null pointers: absence is
   always a *value*.

3. **Probing inspects; mutation takes.** Reads (`probe`, `get`, the shift's
   decision) hold the map `const` and unwrap through a view —
   `(&slot.entry)?` binds a const view of the payload, nothing moves. Moves
   happen only through `takeEntry`, whose owned-slot `?` consumes the
   payload and leaves the field **absent** — the `T?` default, a real value
   — never uninitialized. No map code ever reads a vacated slot, and no slot
   is ever vacated.

4. **Growth is the sanctioned invalidation — the whole table moves.** `grow`
   rehashes every live pair into a fresh table; the old table is consumed by
   the reinsert unwraps and dropped **all-absent** at `m.buckets = nb` (note
   5). Two round rulings ride on this: **assignment drops the previous
   occupant in place** (compiler-generated deallocation only — see note 7),
   and a dropped container's leftover state is never inspected — §6's
   repair-before-escape governs the containers that *leave* the function,
   not ones that die in place. And this is C15's rule in its sharpest form:
   buffer growth invalidates outstanding views — an open-addressed table
   relocates its entries, so **any `const *V` view from `get` dangles after
   a growing `put`**. Views into the map are const, short-lived, and never
   held across a mutation.

5. **The old table dies all-absent on purpose.** The reinsert
   `if kv := m.buckets[i].entry?` consumes every live slot's payload (owned
   unwrap), leaving each field **absent** — the `T?` default; empty slots
   were already absent. So when `m.buckets = nb` drops the old table, no
   slot is uninitialized and no payload is orphaned: this is the C19 point
   for a long-lived table — absence is the shape's default state, so the
   vacated-slot discipline of §6 is never needed here. (`MERGE_SORT.md`'s
   scratch buffer stays the vacated-state example.)

6. **Backward-shift deletion is arithmetic, not comparison.** The shift test
   is pure `dist` arithmetic: `dh == 0 || dh > dr` — the entry at `r` moves
   into the hole exactly when its home is not strictly between them going
   forward, so every surviving key stays reachable from its home. Key
   equality itself is `infix_operator==` — the `find` operator of §4 —
   nothing else. Coalesced clusters get no tombstones: the trailing slot is
   cleared back to the default absence (`m.buckets[hole].entry = {}`), so a
   probing `get` never needs to skip graves.

7. **No disposable K/V.** "Assignment drops the previous occupant" means
   *compiler-generated* deallocation: `put`'s replace path and `grow`'s drop
   rely on it. A type with a user `dispose` must be discharged along an
   explicit, exactly-once path — which a table slot cannot promise — so
   disposable element types are out of scope, the same carve the JSON-shaped
   constraint makes (C12).

8. **`loop e in m` is deliberately omitted.** The `begin`/`end`/`next`/
   `current` protocol demands `current &Entry` — a *writable* view of a
   payload living inside a `T?` field — and there is no spelling for that:
   `&x?` unwraps through a view to a **const** payload view (C19), and an
   owned unwrap consumes the slot. So the const walk in the usage section is
   the documented iteration, and a const-current variant of the protocol is
   the future door.

9. **The load contract and uint guards.** Growth fires at `count * 4 >=
   length * 3` (≈ 0.75), which keeps probe walks short and guarantees a free
   slot inside every probe — `probe`'s `TableFullError` is a typed backstop,
   unreachable in a contract-abiding table. The callers map it locally:
   get/remove read absence, put skips the insert. All index arithmetic is
   modulo `length`; `dist` is wrap-safe by construction
   (`b + len - a < 2·len`); the load test, the doubling, and `pos + 1`
   overflow only past ~2^62 entries.