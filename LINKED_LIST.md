# Doubly Linked List

A generic doubly linked list, written only with what the language already has:
sentinels instead of null pointers, `&`-allocations into the caller's arena,
the `begin` / `end` / `next` / `current` protocol that backs `loop ... in`,
and `Optional` handled the Haskell way — **stay in the context** with
`map` / `andThen` / `orElse`, unwrap only at the end.

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

// Empty list is a data condition, not a bug -> Optional, no panic inside.
popFront func [T] (list *Head[T]) Optional[T] = {
  if list.length == 0 then
    return None
  first *Node[T] = list.sentinel.next
  list.sentinel.next = first.next
  first.next.prev = &list.sentinel
  list.length -= 1
  return Some(first.value)   // T is Copy (see note 4), so this is a copy, not a move
}

popBack func [T] (list *Head[T]) Optional[T] = {
  if list.length == 0 then
    return None
  last *Node[T] = list.sentinel.prev
  list.sentinel.prev = last.prev
  last.prev.next = &list.sentinel
  list.length -= 1
  return Some(last.value)
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
// Out of range is a data condition -> Optional, no panic inside.
getAt func [T] (list *Head[T], i uint) Optional[*T] = {
  if i >= list.length then
    return None
  node *Node[T] = list.sentinel.next
  loop _ in 0..<i do
    node = node.next
  return Some(&node.value)
}

// `Optional` is a `const enum [T any]`, so the payload may be a pointer.
find func [T] (list *Head[T], v T) Optional[*T] = {
  node *Node[T] = list.sentinel.next
  loop node != &list.sentinel {
    if node.value == v then
      return Some(&node.value)
    node = node.next
  }
  return None
}

// Removal needs the node, so `findNode` is the node-level query; element
// views cannot feed `remove`.
findNode func [T] (list *Head[T], v T) Optional[*Node[T]] = {
  node *Node[T] = list.sentinel.next
  loop node != &list.sentinel {
    if node.value == v then
      return Some(node)
    node = node.next
  }
  return None
}

// Passing the sentinel here is an invariant violation, a bug — panic stays.
remove func [T] (list *Head[T], node *Node[T]) T = {
  if node == &list.sentinel then
    panic("cannot remove the sentinel")
  node.prev.next = node.next
  node.next.prev = node.prev
  list.length -= 1
  return node.value   // copy, see note 4
}
```

## Optional combinators (standard library)

The Haskell trio: `fmap`, `>>=` (bind), `fromMaybe`. `match` lives *inside*
these once; user code that composes values almost never needs it.

```c
map func [A, B] (opt Optional[A], f func (A) B) Optional[B] = {
  match opt {
    Some(v) => Some(f(v))
    None    => None
  }
}

andThen func [A, B] (opt Optional[A], f func (A) Optional[B]) Optional[B] = {
  match opt {
    Some(v) => f(v)
    None    => None
  }
}

orElse func [A] (opt Optional[A], fallback A) A = {
  match opt {
    Some(v) => v
    None    => fallback
  }
}
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

Every fallible step below is composed with combinators — **no `match`, no
`panic`** in user code. A trailing-block lambda binds a single parameter as
`it` (reserved inside the body); several declare their own names before `:` —
so `l.find(20).map { it * 2 }` is the lambda `func (A) B` passed as
`map`'s last argument `f`, spelled explicitly if you prefer:
`l.find(20).map(f = func (v A) do v * 2)`.
A is `*int` here — `find` hands out an element view, and operators
auto-dereference views (README, auto dereference).

```c
l *Head[int] = newList()
l.pushBack(10)
l.pushBack(20)
l.pushFront(5)   // l is now: 5, 10, 20

loop e in l do
  out.println("%d{e}")       // 5, 10, 20 — e is a view, nothing is copied

// map: transform inside the context — Haskell fmap
doubled Optional[int] = l.find(20)
  .map { it * 2 }               // it: element view *int — operators auto-deref
out.println("doubled = %d{doubled.orElse(-1)}")   // 40; -1 if 20 were missing

// fromMaybe: unwrap with a default, never a panic
label string = l.find(99)
  .map { "%d{it}" }             // formatting auto-derefs the view, like `e` above
  .orElse("99 is not in the list")
out.println(label)            // "99 is not in the list"

// bind-like chain: findNode -> remove in one expression (Haskell `fmap (remove l) (find l t)`)
popValue func [T] (l *Head[T], target T) Optional[T] = {
  l.findNode(target)
    .map { l.remove(it) }
}

popped int = l.popValue(10)
  .orElse(0)   // 10 removed from l; 0 if absent
// l is now: 20

first int = l.popFront()              // decay: Some(20), unwraps; pans only if empty

// true branching still exists when you want it:
match l.popValue(20) {
  Some(v) => out.println("still there: %d{v}")
  None    => out.println("20 is gone")
}
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

p1 *Point = points.getAt(0)           // decay; pans if the index were out of range
p1.x = 0                              // writable view of the element — affects the list
p1.y = 0

match points.find(Point {2, 2}) {
  Some(n) => out.println("found Point {%d{n.x}, %d{n.y}}")
  None    => out.println("no such point")
}
```

## Proposal: monadic block (do-notation)

Deep chains of `.andThen` would get noisy. Haskell solves that with `do x <- m;
...`, desugaring to `>>=`. Sketch for this language — the keyword is undecided
(`do` already means one-line function/loop bodies):

```c
// PROPOSAL — same desugaring as Haskell's `do`
maybe {                              // or: opt { }, chain { } ...
  node <- l.findNode(20)             // unwrap; absence short-circuits the block to None
  l.remove(node)                     // last bare value lifts into Some
}
// == l.findNode(20).andThen { Some(l.remove(it)) }
```

## Notes

1. **Composers vs. unwrappers — four tools, one ladder.** `map`/`andThen` stay
   in the context (absent-safe), `orElse` unwraps with a default (no panic),
   decay unwraps asserting presence (pans on `None`), and `match` branches.
   Decay is contextual: it fires only when the surrounding type is pinned to
   exactly `T` (typed binding, typed argument, `return`, operator operand,
   field initializer) and never to satisfy inference; it peels exactly one
   layer and reads Copy payloads or moves heap payloads (second use of a
   consumed binding is a compile error).

2. **The combinators bind the payload by value.** `map`/`andThen`/`orElse`
   take the payload as `v A` — a Copyable payload is copied in, a heap
   payload is *moved* in (value parameters move non-Copyable values — README,
   "Copyable types"), so `Optional[string]` chains work as well as
   `Optional[int]`. The box itself is consumed by `match` (README,
   "Consumption").

3. **Two failure categories.** Data-dependent absence (empty list, index out
   of range, not found) is `Optional` — the caller picks `map`, `orElse`,
   decay, or `match`. Invariant violations (removing the sentinel, invalid
   arguments) panic — they are bugs, not data. `getAt`, `find`, `popFront`,
   `popBack`, and consequently all user code above carry no match and no
   panic.

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
   construction, so the placeholder is never dereferenced. (b) `Some` can
   carry a pointer because `Optional const enum [T any]` accepts any `T`,
   and matching a `const` `Optional` inspects without consuming.

7. **Function bodies are not arenas.** `&`-allocations inside a function body
   land in the nearest enclosing *statement-block* arena — the caller's
   (README "Where memory lives") — this is a rule, not an assumption. So
   `newList` may return a node chain and `toJson` may `Ok(json)`: the objects
   outlive the call. Owned locals still get their per-variable deallocation
   at function end (R1).

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
   `find` return `Optional[*T]` — a writable view of the *element* (never a
   copy: a copy would only be legal for Copy `T`, note 4, and would pay a
   full element copy for zero aliasing gain). `remove` takes a node, so the
   node-level query is `findNode` → `Optional[*Node[T]]`. The checker does no
   alias analysis, so `remove` must panic on a node that is not a member of
   this list's chain — a foreign node or an already-removed one is a bug, and
   a silent unlink corrupts the chain. Keep the sentinel guard; extend it to
   the membership check.

10. **Sharing vs owning resources in a list.** `dispose` makes a type
    non-Copy (`## Resources` in README), so a resource-owning element cannot
    be *copied*, but it *can* be moved in (`pushBack` moves by value). The
    list then owns the payloads and must discharge them —
    `Head[T]`/`Node[T]` become disposable and the synthesized dispose walks
    the live nodes (`OWNERSHIP_DRAFT.md`, missing rules — linked list #4).
    For resources with external owners the simpler shape stays `*T` views
    (pointers are Copy) and the real owner disposes; arena bulk-free covers
    every node.

11. **Heap elements work through moves and `take`.** `pushBack(v)` stores `v`
    by *move* into the node, so heap element types need no Copy. Reading and
    mutating work through views (`const *T` / `&T`) as before. Extraction is
    where the Copy-only sketch stops (`Some(first.value)` is a copy), and
    that is what `take` replaces:

    ```c
    take func [T] (n *Node[T]) T    // relocates the payload out, consumes n

    popFront func [T] (l *Head[T]) Optional[T] = {
      if l.length == 0 then
        return None
      first *Node[T] = l.sentinel.next
      l.sentinel.next = first.next
      first.next.prev = &l.sentinel
      l.length -= 1
      Some(first.take())            // `first` is dead afterwards
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
    the slot-take rule (`OWNERSHIP_DRAFT.md`, missing rules — linked list
    #6; QUICK_SORT note 1). An unlinked node's location is unreachable, so
    reinit never applies there — the two forms are one rule, two states.
