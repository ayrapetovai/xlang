# Doubly Linked List

A generic doubly linked list, written only with what the language already has:
sentinels instead of null pointers, `&`-allocations into the caller's arena,
the `begin` / `end` / `next` / `current` protocol that backs `loop ... in`,
and absence handled **by form** — the checked `if x := …?` heads, `?` / `??`,
with no `match` on the shape (C19: `match` on a `T?` is a compile error).

## Shape

There are no null pointers, so an empty list is a **self-looping sentinel**:
`sentinel.next` and `sentinel.prev` both point at the sentinel itself. A node
is always some real element; the sentinel is "past the end".

```c
Node struct [T] = {
  value T
  prev  *Node[T]
  next  *Node[T]
}

Head struct [T] = {
  sentinel Node[T]  // `value` is unused; prev/next wrap the list around
  length   uint
}

ListIterator struct [T] = {
  node *Node[T]     // == &head.sentinel  means "past the end"
}
```

## Construction and mutation

`&`-allocations inside a function body land in the **caller's arena**, so the
`Head` and all `Node`s created below outlive the call and are freed together
when the owning block exits. No node is ever freed individually — clearing is
just rewiring the sentinel, and the arena takes care of the rest.

```c
newList func [T] () *Head[T] = {
  head := &Head[T] {
    length = 0
  }
  head.sentinel.prev = &head.sentinel
  head.sentinel.next = &head.sentinel
  return head
}

pushBack func [T] (list *Head[T], v T) = {
  last *Node[T] = list.sentinel.prev
  node := &Node[T] {
    value = v
    prev  = last
    next  = &list.sentinel
  }
  last.next = node          // node.prev already == last
  list.sentinel.prev = node
  list.length += 1
}

pushFront func [T] (list *Head[T], v T) = {
  first *Node[T] = list.sentinel.next
  node  := &Node[T] {
    value = v
    prev  = &list.sentinel
    next  = first
  }
  first.prev = node         // node.next already == first
  list.sentinel.next = node
  list.length += 1
}

// Empty list is a data condition, not a bug -> T? absence, no panic inside.
popFront func [T] (list *Head[T]) T? = {
  if list.length == 0 then
    return
  first *Node[T] = list.sentinel.next
  list.sentinel.next = first.next
  first.next.prev = &list.sentinel
  list.length -= 1
  return first.value   // T is Copy (see note 4), so this is a copy, not a move
}

popBack func [T] (list *Head[T]) T? = {
  if list.length == 0 then
    return
  last *Node[T] = list.sentinel.prev
  list.sentinel.prev = last.prev
  last.prev.next = &list.sentinel
  list.length -= 1
  return last.value
}

clear func [T] (list *Head[T]) = {
  list.sentinel.prev = &list.sentinel
  list.sentinel.next = &list.sentinel
  list.length = 0
  // orphaned nodes are still in the arena; they die with the block
}
```

## Queries

```c
// Out of range is a data condition -> T? absence, no panic inside.
getAt func [T] (list *Head[T], i uint) *T? = {
  if i >= list.length then
    return
  node *Node[T] = list.sentinel.next
  loop _ in 0..<i do
    node = node.next
  return &node.value
}

// the `*T` payload is a writable view into the node — postfix shapes wrap
// any payload type, a pointer included (note 6).
find func [T] (list *Head[T], v T) *T? = {
  node *Node[T] = list.sentinel.next
  loop node != &list.sentinel {
    if node.value == v then
      return &node.value
    node = node.next
  }
  return          // not found — absence
}

// Removal needs the node, so `findNode` is the node-level query; element
// views cannot feed `remove`.
findNode func [T] (list *Head[T], v T) *Node[T]? = {
  node *Node[T] = list.sentinel.next
  loop node != &list.sentinel {
    if node.value == v then
      return node
    node = node.next
  }
  return          // not found — absence
}

NotInListError error = { message string }

// Removing the sentinel is an invariant violation — reported, not panicked
// (the full membership check, note 9, is the documented extension).
remove func [T] (list *Head[T], node *Node[T]) T! = {
  if node == &list.sentinel then
    return NotInListError { message = "node is not a member of this list" }
  node.prev.next = node.next
  node.next.prev = node.prev
  list.length -= 1
  return node.value   // copy, see note 4
}
```

## Absence handling (C19 forms)

The old Haskell trio (`map` / `andThen` / `orElse`) is gone: each one
`match`es the shape, and `match` on a `T?` is a compile error (C19). The
language's handling forms replace them — user code that composes values
uses these, and `match` never appears:

```c
//  - `if x := e? then … else …` — branch on presence; the payload binds
//  - `loop x := e? do …` — the same branching head for loops
//  - `e ?? default` — fall back and keep going
//  - `e == {}` / `e != {}` — the presence tests, anywhere
//  - `(&e)?` — unwrap through a view; binds a const view of the payload
```

## Iteration protocol

These four functions make `loop e in list` work. The iterator is a tiny
struct, so `l.begin() != l.end()` compares it **structurally** (a single pointer
field — node identity, no nulls to trip over).

```c
begin func [T] (list *Head[T]) ListIterator[T] = {
  ListIterator[T] {
    node = list.sentinel.next
  }
}

end func [T] (list *Head[T]) ListIterator[T] = {
  ListIterator[T] {
    node = &list.sentinel
  }
}

next func [T] (it ListIterator[T]) ListIterator[T] = {
  ListIterator[T] {
    node = it.node.next
  }
}

current func [T] (it ListIterator[T]) &T = {
  &it.node.value   // a writable view into the node, per the `current &E` contract
}
```

## Usage

Every fallible step below is handled by form — **no `match`, no `panic`** in
user code: the checked `if …?` binds, `??` falls back. `find` hands out an
element view (`*int`), and operators auto-dereference views (README, auto
dereference).

```c
l *Head[int] = newList()
l.pushBack(10)
l.pushBack(20)
l.pushFront(5)   // l is now: 5, 10, 20

loop e in l do
  out.println("%d{e}")       // 5, 10, 20 — e is a view, nothing is copied

// the checked form — presence binds the payload, absence runs the else:
if v := l.find(20)? then
  out.println("doubled = %d{v * 2}")      // 40 — v: element view *int, auto-deref
else
  out.println("20 missing")

// fallback — unwrap with a default, never a panic (the old `orElse`):
label string
if v := l.find(99)? then
  label = "%d{v}"
else
  label = "99 is not in the list"
out.println(label)            // "99 is not in the list"

// absence is data, not a failure — popValue returns T?; a bare return is
// absence, and remove's failure (unreachable here: findNode never yields
// the sentinel) maps to absence through the catch:
popValue func [T] (l *Head[T], target T) T? = {
  if n := l.findNode(target)? then {
    try return l.remove(n)      // the guard unwraps the `T!`
    catch _                     //   failure — mapped to absence
    return
  } else
    return                      // not found — no match, no panic
}

popped int = l.popValue(10) ?? 0   // 10 removed from l; l is now: 5, 20

first int = l.popFront() ?? 0   // 5 — the front after the pop; `??` keeps going
// l is now: 20

// true branching still exists when you want it:
if v := l.popValue(20)? then
  out.println("still there: %d{v}")   // 20 was still there
else
  out.println("20 is gone")
```

Generic element types are Copyable values:

```c
Point struct = {
  x int
  y int
}

// operator overloading makes `find` work on structs too
infix_operator== func (a const *Point, b const *Point) bool = {
  a.x == b.x && a.y == b.y
}

points *Head[Point] = newList()
points.pushBack(Point {3, 4})
points.pushBack(Point {1, 2})

loop p in points do
  out.println("(%d{p.x}, %d{p.y})")   // (3, 4) then (1, 2)

// getAt hands out a writable view into the list element:
if p1 := points.getAt(0)? then {      // p1: *Point — a writable view into the node
  p1.x = 0                            // affects the list
  p1.y = 0
}

if p := points.find(Point {2, 2})? then
  out.println("found Point {%d{p.x}, %d{p.y}}")
else
  out.println("no such point")
```

## Proposal: monadic block (do-notation)

Deep chains of nested checked `if …?` forms would get noisy. Haskell solves
that with `do x <- m; ...`, desugaring to `>>=` — this language has no
combinators (note 1), so the block desugars to a checked-if chain. Sketch —
the keyword is undecided (`do` already means one-line function/loop bodies):

```c
// PROPOSAL — desugars to a checked-if chain; the guard reads the `T!`
maybe {                              // or: opt { }, chain { } ...
  node <- l.findNode(20)             // unwrap; absence short-circuits the block to absence
  l.remove(node)                     // last bare value lifts into the `T?` — the `T!`
}                                    //   result is read by the guard; failure maps to absence
// == inside a T? function:
//    if n := l.findNode(20)? then {
//      try return l.remove(n)        // unwrap the `T!`; failure lands in the handler
//      catch _
//      return                        //   and maps to absence
//    } else
//      return                        // not found — absence
```

## Notes

1. **Handling by form, not by arms.** The C19 shapes are unmatchable
   (`match` on a `T?`/`T!` value is a compile error — §8), so absence is
   handled by form: the checked heads (`if x := e? then … else`, `loop x :=
   e? do`) branch on presence, `?? default` falls back and keeps going, the
   presence tests `x == {}` / `x != {}` ask directly, and `(&e)?` unwraps
   through a view to a const payload view. There is no `Some`/`None` to
   match and no decay — the old `map`/`andThen`/`orElse` combinators would
   need `match` on the shape, so they are gone from the language (the
   section above lists what replaced them). This sketch's user code carries
   no match and no panic, exactly as before.

2. **Unwrap is a consume.** `?` / `??` move the payload out of the shape: a
   Copyable payload copies, a heap payload *moves* — so a box cannot be
   unwrapped twice (README, ``## `T?` and `T!` ``). Unwrapping through a
   *view* (`(&e)?`) binds a const view instead. This sketch unwraps owned
   payloads only in `popFront`/`popBack` (`return first.value`); `getAt` /
   `find` / `findNode` hand out `*T` / `*Node[T]` payloads — pointers are
   Copy, so the checked head binds the view and the box is gone.

3. **Failure and absence categories.** Data-dependent absence (empty list,
   index out of range, not found) is `T?` — the caller picks the checked
   form, `??`, or the presence tests. Invariant violations (removing the
   sentinel, invalid arguments) are bugs, not data: an author may report
   them as declared `T!` failures with an error kind (`remove` returns
   `NotInListError`) or panic. `getAt`, `find`, `popFront`, `popBack`, and
   consequently all user code above carry no match and no panic.

4. **`value T` parameters and returns move.** A `T` value parameter copies in
   Copyable types and *moves in* heap types (README, "Copyable types"), so
   `pushBack(v T)` stores a heap `v` by move. Extraction is the limit: a
   plain `node.value` expression *copies*, legal only for Copyable `T` —
   heap payloads leave the list via `take` (note 11). `Head[int]`,
   `Head[Point]` qualify as is.

5. **Iterator `!=` is structural.** `ListIterator` has one pointer field, so
   `l.begin() != l.end()` compares node addresses — every pointer is non-null,
   sentinel included, so equality is total and well-defined.

6. **Two props the example leans on.** (a) A pointer-typed field omitted in a
   struct literal defaults to the address of the pointee type's shared zero
   instance (never null); we override the sentinel's links right after
   construction, so the placeholder is never dereferenced. (b) the postfix
   shapes wrap any payload, pointer included — `*T?` is how `find` returns a
   writable element view; the checked head unwraps the box (the pointer
   itself is Copy) without touching the node.

7. **Function bodies are not arenas.** `&`-allocations inside a function body
   land in the nearest enclosing *statement-block* arena — the caller's
   (README "Where memory lives") — this is a rule, not an assumption. So
   `newList` may return a node chain and `toJson` may `return json`: the
   objects outlive the call. Owned locals still get their per-variable
   deallocation at function end (R1).

8. **`newList` returns `*Head[T]`, never a `Head` value.** `Head` is Copyable
   (inline `Node` + `uint`), so returning it by value would hand out a value
   whose sentinel carries `prev`/`next` pointers into one chain — copying it
   (`h2 : h1`) would give two "owners" of the same list, the shared-mutable
   case the ownership model forbids, and the checker cannot catch it (not a
   string/`[]T` heap type). Keeping the whole `Head` behind `&` in the arena
   means only views `*Head[T]` ever exist, and copying a *view* is harmless.
   General rule: a Copyable struct that owns through pointers must never
   exist as a value.

9. **Queries return views, mutators must not trust their input.** `getAt` /
   `find` return `*T?` — a writable view of the *element* (never a copy: a
   copy would only be legal for Copy `T`, note 4, and would pay a full
   element copy for zero aliasing gain). `remove` takes a node, so the
   node-level query is `findNode` → `*Node[T]?`. The checker does no alias
   analysis, so `remove` reports a node that is not a removable element —
   the sentinel, a foreign node, or an already-removed one is a bug, and a
   silent unlink would corrupt the chain. The code guards the sentinel
   (`NotInListError`); extending the guard to a full membership walk is the
   documented direction.

10. **Sharing vs owning resources in a list.** `dispose` makes a type
    non-Copy (`## Resources` in README), so a resource-owning element cannot
    be *copied*, but it *can* be moved in (`pushBack` moves by value). The
    list then owns the payloads and must discharge them —
    `Head[T]`/`Node[T]` become disposable and the synthesized dispose walks
    the live nodes (`OWNERSHIP_RULES.md` §1 — disposable types discharge
    exactly once).
    For resources with external owners the simpler shape stays `*T` views
    (pointers are Copy) and the real owner disposes; arena bulk-free covers
    every node.

11. **Heap elements work through moves and `take`.** `pushBack(v)` stores `v`
    by *move* into the node, so heap element types need no Copy. Reading and
    mutating work through views (`const *T` / `&T`) as before. Extraction is
    where the Copy-only sketch stops (`return first.value` is a copy), and
    that is what `take` replaces:

    ```c
    take func [T] (n *Node[T]) T    // relocates the payload out, consumes n

    popFront func [T] (l *Head[T]) T? = {
      if l.length == 0 then
        return
      first *Node[T] = l.sentinel.next
      l.sentinel.next = first.next
      first.next.prev = &l.sentinel
      l.length -= 1
      return first.take()           // auto-wrap; `first` is dead afterwards
    }
    ```

    `take` relocates the payload's backing into the caller's (current) arena
    — the same machinery as a channel receive — and *consumes the node*: it
    must be unlinked first, and any later read of `first` is
    use-after-consume. The sentinel is never taken.

    That dead-node reading is the *unlinked-node* form of `take`. On a live
    container slot (`take(a[i])`, or the implicit `t := a[i]` move-out) the
    same machinery leaves the slot **uninitialized**, not R4-consumed: reads
    are compile errors until a move-in (`a[i] = v`) **reinitializes** it —
    the slot-take rule (`OWNERSHIP_RULES.md` §6; QUICK_SORT note 1). An unlinked node's location is unreachable, so
    reinit never applies there — the two forms are one rule, two states.
