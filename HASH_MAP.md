# Hash Map

A generic open-addressed hash map. Written only with what the language
already has: `&`-created containers in the arena, `Optional` as the honest
"maybe", the `+=` move-append family, `infix_operator==` for key equality,
and — the one mechanism this sketch establishes (ruling C16) — a **`hash`
function resolved per instantiation**, exactly like the ordering operators:
`hash func (k const *K) uint`.

There are **no null pointers**, so an empty bucket cannot be a `nil` pointer
and (worse) cannot be a *vacated* slot — a vacated slot is an unreadable,
tracked non-value (`OWNERSHIP_RULES.md` §6), the wrong thing for a
long-lived table. The sketch's answer: **open addressing over
`Slot { entry Optional[Entry] }`** — an empty slot is the real value `None`.
Every slot is always some value; only `count` says how many are live.

## Shape

```c
Entry struct [K, V] = {
  key K
  val V
}

// an empty slot is a real value — `None` — never a null and never a vacated
// slot; the table holds only full Some/None values (note 2)
Slot struct [K, V] = {
  entry Optional[Entry[K, V]]
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
// through a const view, so the match *inspects* — nothing moves (note 3).
probe func [K, V] (m const *HashMap[K, V], key const *K) Probe = {
  pos := hash(key) % m.buckets.length
  scanned uint = 0
  loop scanned < m.buckets.length {
    match m.buckets[pos].entry {
      None     => return Probe { pos = pos, found = false }
      Some(kv) => if kv.key == key then
                    return Probe { pos = pos, found = true }
    }
    pos = (pos + 1) % m.buckets.length
    scanned += 1
  }
  panic("hash table full")   // unreachable: the load contract keeps a free slot
}

// first free slot for `key` — terminates because the table is never full
probeEmpty func [K, V] (b const *[]Slot[K, V], key const *K) uint = {
  pos := hash(key) % b.length
  loop {
    match b[pos].entry {
      None => return pos
      Some(_) => {}
    }
    pos = (pos + 1) % b.length
  }
}

// a view into the stored value — read-only by construction, invalidated by
// growth (note 4). Absence is data: Optional, never Result.
get func [K, V] (m const *HashMap[K, V], key const *K) Optional[const *V] = {
  if m.buckets.length == 0 then
    return None
  p := m.probe(key)
  if !p.found then
    return None
  match m.buckets[p.pos].entry {      // const map: inspection, no consumption
    Some(kv) => return Some(&kv.val)  // const *V into the map's slot
    None     => panic("probe found a free slot at its pos")   // unreachable
  }
}
```

## Mutation

```c
// move the pair out of a live slot; the slot is left *uninitialized* — the
// caller must reinitialize it before the map escapes (put and remove do)
takeEntry func [K, V] (m *HashMap[K, V], pos uint) Entry[K, V] = {
  match m.buckets[pos].entry {        // owned slot: the match consumes it
    Some(kv) => return kv
    None     => panic("takeEntry on a free slot")      // invariant violation
  }
}

put func [K, V] (m *HashMap[K, V], key K, val V) = {
  if m.buckets.length == 0 then
    m.grow()                          // belt and braces; newHashMap already sized
  p := m.probe(key)
  if p.found then {
    old := m.takeEntry(p.pos)         // move out — the slot is now uninitialized
    m.buckets[p.pos].entry = Some(Entry { key = key, val = val })   // reinit
    // `old` is deallocated at this block's exit — built-in machinery only:
    // K and V may not define `dispose` (note 7)
  } else {
    if m.count * 4 >= m.buckets.length * 3 then {   // load ≈ 0.75 → grow first
      m.grow()
    }
    pos := m.probeEmpty(key)
    m.buckets[pos].entry = Some(Entry { key = key, val = val })  // over a `None`
    m.count += 1
  }
}

// grow: a fresh table of twice the size, rehashed by moves. The old table is
// consumed slot-by-slot and dropped all-vacated at the assignment (notes 4, 5).
grow func [K, V] (m *HashMap[K, V]) = {
  newLen := m.buckets.length * 2
  if newLen < 16 then
    newLen = 16
  nb []Slot[K, V] = {}
  loop i := 0; i < newLen {
    nb += Slot { entry = None }       // move-append: every slot starts real
    i += 1
  }
  loop i := 0; i < m.buckets.length {
    match m.buckets[i].entry {
      Some(kv) => {
        pos := nb.probeEmpty(kv.key)  // nb is fresh — no conflicts, no replace
        nb[pos].entry = Some(kv)      // move-in
      }
      None => {}
    }
    i += 1
  }
  m.buckets = nb          // drops the old (all-vacated) table in place
}

// backward-shift deletion: pull each following entry left that may (its home
// is not strictly between hole and r), until the first free slot; the
// trailing vacated slot becomes `None`. Every surviving key stays reachable
// from its home (note 6).
remove func [K, V] (m *HashMap[K, V], key const *K) Optional[V] = {
  if m.buckets.length == 0 then
    return None
  p := m.probe(key)
  if !p.found then
    return None
  e := m.takeEntry(p.pos)             // the removed pair; p.pos vacated
  len := m.buckets.length
  hole := p.pos
  r := (p.pos + 1) % len
  loop r != p.pos {
    match &m.buckets[r].entry {       // inspect — decide without moving
      None => break                   // cluster ends
      Some(kv) => {
        dh := dist(hole, hash(kv.key) % len, len)
        dr := dist(hole, r, len)
        if dh == 0 || dh > dr then {  // home not in (hole, r] — may shift left
          e := m.takeEntry(r)         // move it: r is vacated, becomes the hole
          m.buckets[hole].entry = Some(e)
          hole = r
        }
      }
    }
    r = (r + 1) % len
  }
  m.buckets[hole].entry = None        // the trailing vacated slot is free
  m.count -= 1
  return Some(e.val)                  // the key dies with e — dropped at return
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

match m.get("alice") {
  Some(a) => out.println("alice is %d{a}")   // 31 — a: const *int, auto-deref
  None    => out.println("alice missing")
}

removed int = m.remove("bob").orElse(0)  // 40 — the combinators of LINKED_LIST
match m.remove("alice") {
  Some(old) => out.println("alice held %d{old}")   // 31
  None      => out.println("alice was gone")
}

total uint = 0
loop i := 0; i < m.buckets.length {      // const iteration — the documented form
  match m.buckets[i].entry {
    Some(kv) => total += uint(kv.val)
    None     => {}                       // empty arms spell `{}`
  }
  i += 1
}
out.println("total %d{total}")  // 0 — both keys were removed
```

Custom keys need only `==` and `hash` in scope:

```c
m2 *HashMap[Point, string] = newHashMap()
m2.put(Point { x = 1, y = 2 }, "one-two")
m2.put(Point { x = 3, y = 4 }, "three-four")

match m2.get(Point { x = 1, y = 2 }) {
  Some(s) => out.println("%s{s}")        // "one-two" — s: const *string
  None    => out.println("missing")
}
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

2. **Empty slots are `None`, never vacated.** A vacated slot is a tracked
   non-value; reading it is a compile error until reinitialized (§6) — so a
   table whose "empties" were vacated could never escape. Encoding absence
   as `None` keeps every slot a real value. This is the same reasoning that
   keeps the language free of null pointers: absence is always a *value*.

3. **Probing inspects; mutation takes.** Reads (`probe`, `get`) go through
   const views, so their matches inspect without consuming; moves happen only
   through `takeEntry` and the shift's explicit take — slot-take, followed by
   an immediate move-in. No map code ever reads a vacated slot, and a vacated
   slot never survives to the map's escape.

4. **Growth is the sanctioned invalidation — the whole table moves.** `grow`
   rehashes every live pair into a fresh table; the old table is consumed by
   the reinsert match and dropped all-vacated at `m.buckets = nb`. Two round
   rulings ride on this: **assignment drops the previous occupant in place**
   (compiler-generated deallocation only — see note 7), and **a dropped
   container may hold vacated slots** — §6's repair-before-escape governs the
   containers that *leave* the function, not ones that die in place. And this
   is C15's rule in its sharpest form: buffer growth invalidates outstanding
   views — an open-addressed table relocates its entries, so **any `const *V`
   view from `get` dangles after a growing `put`**. Views into the map are
   const, short-lived, and never held across a mutation.

5. **The old table dies all-vacated on purpose.** The reinsert match
   consumes every slot (`match x consumes`), Some arms included, so the old
   `buckets` array is entirely uninitialized when it is dropped. That is
   fine — exactly the shape of `MERGE_SORT.md`'s scratch buffer, which also
   dies vacated: the repair discipline is about escaping, not about dying
   locals (note 4).

6. **Backward-shift deletion is arithmetic, not comparison.** The shift test
   is pure `dist` arithmetic: `dh == 0 || dh > dr` — the entry at `r` moves
   into the hole exactly when its home is not strictly between them going
   forward, so every surviving key stays reachable from its home. Key
   equality itself is `infix_operator==` — the `find` operator of §4 —
   nothing else. Coalesced clusters get no tombstones: the last shifted slot
   becomes `None`, so a probing `get` never needs to skip graves.

7. **No disposable K/V.** "Assignment drops the previous occupant" means
   *compiler-generated* deallocation: `put`'s replace path and `grow`'s drop
   rely on it. A type with a user `dispose` must be discharged along an
   explicit, exactly-once path — which a table slot cannot promise — so
   disposable element types are out of scope, the same carve the JSON-shaped
   constraint makes (C12).

8. **`loop e in m` is deliberately omitted.** The `begin`/`end`/`next`/
   `current` protocol demands `current &Entry` — a *writable* view of a
   payload living inside an `Optional` slot — and matching an owned slot
   consumes it. There is no spelling for a writable view into a `Some`'s
   payload; the const walk in the usage section is the documented iteration,
   and a const-current variant of the protocol is the future door.

9. **The load contract and uint guards.** Growth fires at `count * 4 >=
   length * 3` (≈ 0.75), which keeps probe walks short and guarantees a free
   slot inside every probe — the `panic("hash table full")` sites are
   unreachable. All index arithmetic is modulo `length`; `dist` is
   wrap-safe by construction (`b + len - a < 2·len`); the load test, the
   doubling, and `pos + 1` overflow only past ~2^62 entries.