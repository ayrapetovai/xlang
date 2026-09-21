// ── Doubly linked list with a sentinel ──────────────────────────────
// No null pointers: every list has one always-present sentinel node.
//   h.sentinel.next → first node      h.sentinel.prev → last node
//   empty list: sentinel.next == sentinel.last == sentinel
//
// Memory: nodes are created with `&` in the current arena. Allocations
// inside function bodies go into the CALLER's arena (returned strings and
// arrays already require this, see toJson / string.from), so helpers may
// safely grow the list. Nothing is freed per-node: the whole list dies with
// its arena; remove() only unlinks.

// element
Node : struct [T] {
  value : T
  prev  : *Node [T]
  next  : *Node [T]
}

// the list header; implements the Iterable template (functions below)
Head : struct [T] Iterable [T] {
  sentinel : *Node [T]
  last     : *Node [T]
  length   : uint
}

// state passed through the `loop ... in` protocol
ListIterator : struct [T] {
  node : *Node [T]
}

// ── construction ────────────────────────────────────────────────────
newList : func [T] () Head [T] {
  sentinel : & Node [T] {}
  sentinel.next = sentinel   // non-null: self-link before use
  sentinel.prev = sentinel
  Head [T] {
    sentinel : sentinel
    last     : sentinel
    length   : 0
  }
}

// ── mutation ────────────────────────────────────────────────────────
pushBack : func [T] (h : *Head [T], value : T) {
  n : & Node [T] {
    value : value
    prev  : h.last
    next  : h.sentinel
  }
  h.last.next = n           // == h.sentinel.next when the list was empty
  h.sentinel.prev = n
  h.last = n
  h.length += 1
}

pushFront : func [T] (h : *Head [T], value : T) {
  n : & Node [T] {
    value : value
    prev  : h.sentinel
    next  : h.sentinel.next
  }
  h.sentinel.next.prev = n
  h.sentinel.next = n
  if h.length == 0 then
    h.last = n
  h.length += 1
}

popFront : func [T] (h : *Head [T]) Optional [T] {
  if h.length == 0 then
    return None
  n : h.sentinel.next
  n.next.prev = h.sentinel
  h.sentinel.next = n.next
  if h.last == n then
    h.last = h.sentinel
  h.length -= 1
  return Some(n.value)
}

popBack : func [T] (h : *Head [T]) Optional [T] {
  if h.length == 0 then
    return None
  n : h.last
  n.prev.next = h.sentinel
  h.sentinel.prev = n.prev
  h.last = n.prev
  h.length -= 1
  return Some(n.value)
}

clear : func [T] (h : *Head [T]) {
  h.sentinel.next = h.sentinel
  h.sentinel.prev = h.sentinel
  h.last = h.sentinel
  h.length = 0
}

// ── access ──────────────────────────────────────────────────────────
getAt : func [T] (h : *Head [T], index : uint) Optional [T] {
  if index >= h.length then
    return None
  n : h.sentinel.next
  loop i in 0..<index do
    n = n.next
  return Some(n.value)
}

// eq defines element equality; returns a view of the node, not a copy
find : func [T] (h : *Head [T], value : T,
                 eq  : func (a : T, b : T) bool) Optional [*Node [T]] {
  it : begin(h)
  loop it != end(h) do {
    if eq(current(it), value) then
      return Some(it.node)
    it = next(it)
  }
  return None
}

remove : func [T] (h : *Head [T], n : *Node [T]) { // n must belong to h
  if h.length > 0 then {
    n.prev.next = n.next
    n.next.prev = n.prev
    if h.last == n then
      h.last = n.prev
    h.length -= 1
  }
}

// ── `loop ... in` protocol (Iterable) ───────────────────────────────
begin : func [Head[E]] (h : *Head [E]) ListIterator [E] do
  ListIterator {
    node : h.sentinel.next
  }

end : func [Head[E]] (h : *Head [E]) ListIterator [E] do
  ListIterator {
    node : h.sentinel      // one past the last element
  }

next : func [Head[E]] (it : ListIterator [E]) ListIterator [E] do
  ListIterator {
    node : it.node.next
  }

current : func [T] (it : ListIterator [T]) &T do
  it.node.value            // view into the node, no copy

// ── usage ───────────────────────────────────────────────────────────
ints : Head [int] newList ()

ints.pushBack(10)
ints.pushBack(20)
ints.pushFront(5)                    // -> [5, 10, 20]

out.println("length: %d{ints.length}")            // length: 3

loop v in ints do                     // in-protocol iteration
  out.println("  %d{v}")              //   5   10   20

// manual iteration, same as the spec's array example
it : begin(ints)
loop it != end(ints) do {
  out.println("manual: %d{current(it)}")
  it = next(it)
}

match ints.getAt(1) {
  Some(v) => out.println("getAt(1) = %d{v}")     // getAt(1) = 10
  None    => out.println("out of bounds")
}

match ints.find(20, func (a, b) { a == b }) {
  Some(n) => out.println("found %d{n.value}")    // found 20
  None    => out.println("20 is missing")
}

ints.find(99) { a == b }
  .map { out.println("found") }
  .else { out.println("99 is missing") }        // 99 is missing

match ints.popFront() {
  Some(v) => out.println("popped %d{v}")         // popped 5
  None    => out.println("empty")
}

ints.popBack()
  .map { out.println("popped %d{v}") }         // popped 20
  .else { out.println("empty") }

out.println("left: %d{ints.length}")             // left: 1

// a user type that is Copyable (scalars only) works too
Point : struct {
  x : int
  y : int
}

points : Head [Point] newList ()

points.pushBack(Point { x : 1, y : 2 })
points.pushBack(Point { x : 3, y : 4 })
points.pushFront(Point { x : 0, y : 0 })         // -> [(0,0), (1,2), (3,4)]

loop p in points do
  out.println("(%d{p.x}, %d{p.y})")

points.find(Point { x : 3, y : 4 })
           { a.x == b.x && a.y == b.y }
  .map { points.remove(n) }
  .else { out.println("not found") }
}
out.println("after remove: %d{points.length}")   // after remove: 2

