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
  head
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
  Some(first.value)   // T is Copy (see note 4), so this is a copy, not a move
}

popBack func [T] (list *Head[T]) Optional[T] = {
  if list.length == 0 then
    return None
  last *Node[T] = list.sentinel.prev
  list.sentinel.prev = last.prev
  last.prev.next = &list.sentinel
  list.length -= 1
  Some(last.value)
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
getAt func [T] (list *Head[T], i uint) Optional[*Node[T]] = {
  if i >= list.length then
    return None
  node *Node[T] = list.sentinel.next
  loop _ in 0..<i do
    node = node.next
  Some(node)
}

// `Optional` is a `const enum [T any]`, so the payload may be a pointer.
find func [T] (list *Head[T], v T) Optional[*Node[T]] = {
  node *Node[T] = list.sentinel.next
  loop node != &list.sentinel {
    if node.value == v then
      return Some(node)
    node = node.next
  }
  None
}

// Passing the sentinel here is an invariant violation, a bug — panic stays.
remove func [T] (list *Head[T], node *Node[T]) T = {
  if node == &list.sentinel then
    panic("cannot remove the sentinel")
  node.prev.next = node.next
  node.next.prev = node.prev
  list.length -= 1
  node.value   // copy, see note 4
}
```

## Optional combinators (standard library)

The Haskell trio: `fmap`, `>>=` (bind), `fromMaybe`. `match` lives *inside*
these once; user code that composes values almost never needs it.

```c
map func [A, B] (opt Optional[A], f func (v A) B) Optional[B] = {
  match opt {
    Some(v) => Some(f(v))
    None    => None
  }
}

andThen func [A, B] (opt Optional[A], f func (v A) Optional[B]) Optional[B] = {
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
`panic`** in user code. Trailing-block lambdas take their parameter names — and
the named argument — from the function declaration, so call
`l.find(20).map { v.value * 2 }` means call
`l.find(20).map(f = func (v A) do v.value * 2)`, where `f` is the declared
lambda parameter and `v` its declared name.

```c
l *Head[int] = newList()
l.pushBack(10)
l.pushBack(20)
l.pushFront(5)   // l is now: 5, 10, 20

loop e in l do
  out.println("%d{e}")       // 5, 10, 20 — e is a view, nothing is copied

// map: transform inside the context — Haskell fmap
doubled Optional[int] = l.find(20)
  .map { v.value * 2 }
out.println("doubled = %d{doubled.orElse(-1)}")   // 40; -1 if 20 were missing

// fromMaybe: unwrap with a default, never a panic
label string = l.find(99)
  .map { "%d{v.value}" }
  .orElse("99 is not in the list")
out.println(label)            // "99 is not in the list"

// bind-like chain: find -> remove in one expression (Haskell `fmap (remove l) (find l t)`)
popValue func [T] (l *Head[T], target T) Optional[T] = {
  l.find(target)
    .map { l.remove(v) }
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

p1 *Node[Point] = points.getAt(0)     // decay; pans if the index were out of range
p1.value = Point {0, 0}               // writable view into a node, affects the list

match points.find(Point {2, 2}) {
  Some(n) => out.println("found Point {%d{n.value.x}, %d{n.value.y}}")
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
  node <- l.find(20)                // unwrap; absence short-circuits the block to None
  l.remove(node)                    // last bare value lifts into Some
}
// == l.find(20).andThen { Some(l.remove(v)) }
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

2. **The combinators require `A` to be Copy** — `map`/`andThen`/`orElse` bind
   the payload by value (`v A`), valid only for scalars, pointers, and
   Copyable structs/enums. For heap payloads (`Optional[string]`) there is no
   copy: use explicit `match` or view-based variants (`const *A`).

3. **Two failure categories.** Data-dependent absence (empty list, index out
   of range, not found) is `Optional` — the caller picks `map`, `orElse`,
   decay, or `match`. Invariant violations (removing the sentinel, invalid
   arguments) panic — they are bugs, not data. `getAt`, `find`, `popFront`,
   `popBack`, and consequently all user code above carry no match and no
   panic.

4. **`value T` parameters and returns require `T` to be Copy** (scalars,
   pointers, structs/enums built from those). `Head[int]`, `Head[Point]`
   qualify. For heap element types the list still works — access and mutate
   through views (`const *T` / `&T`) instead.

5. **Iterator `!=` is structural.** `ListIterator` has one pointer field, so
   `l.begin() != l.end()` compares node addresses — every pointer is non-null,
   sentinel included, so equality is total and well-defined.

6. **Two props the example leans on.** (a) A pointer-typed field omitted in a
   struct literal defaults to the address of the pointee type's shared zero
   instance (never null); we override the sentinel's links right after
   construction, so the placeholder is never dereferenced. (b) `Some` can
   carry a pointer because `Optional const enum [T any]` accepts any `T`,
   and matching a `const` `Optional` inspects without consuming.

7. **The arena assumption.** This example relies on `&`-allocations inside a
   function body going to the *caller's* arena (they do for `toJson`'s
   `Result[string]`). If that decision changes, `newList` and `pushBack` need
   an explicit pool parameter.

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
   `find` return `Optional[*Node[T]]` — a view into a node, never a copy of
   the element (a copy would only be legal for Copy `T`, note 4, and would
   pay a full element copy for zero aliasing gain). The checker does no alias
   analysis, so `remove` must panic on a node that is not a member of this
   list's chain — a foreign node or an already-removed one is a bug, and a
   silent unlink corrupts the chain. Keep the sentinel guard; extend it to
   the membership check.

10. **Copy-only storage is what keeps the list sound under `dispose`.**
    `value T` parameters/returns and `popFront`'s copy require `T` to be
    Copy (note 4), and `dispose` makes a type non-Copy (`## Resources
    (dispose)` in README) — so a resource-owning element can never be stored
    by value in the list, and no double-close is possible through it. For
    resource elements, store `*T` views (pointers are Copy) and let the real
    owner dispose; arena bulk-free covers every node.
