# Quick Sort

An in-place, generic quick sort, written only with what the language already
has: `&`-method sugar on writable array views, generic functions, the
`loop i <= gt` conditional-loop form, and **scope-based operator overloading**
— the ordering for `quickSort` comes from `infix_operator<` and
`infix_operator==` being in scope for the element type, exactly like
`infix_operator==` powers `find` in the linked list. The sort machinery
takes no comparator parameters: it writes `<` and `==` literally, and the
compiler resolves those operators for `T` at each instantiation.

## The sort

```c
// Swap two slots of an array. `t T` holds the first value: a copy for
// Copyable elements, a move for heap types. swap(a, i, i) is identity —
// the checker elides the move trio, so no slot is ever read back vacated.
swap func [T] (a *[]T, i uint, j uint) = {
  t T = a[i]
  a[i] = a[j]
  a[j] = t
}

// Three-way (Dijkstra) partition. The pivot is the median of {a[lo], a[mid],
// a[hi]}, which keeps sorted and reverse-sorted input at O(n log n) with no
// RNG. The median is *left in its slot* — compared through a const view:
// copying it would MOVE a heap element out of a[hi] and vacate the slot the
// scan reads. Returns the two boundaries of the middle (== pivot) region;
// only the outer two regions are recursed over.
Range struct = { lt uint; gt uint }   // [lo..<lt) < p, [lt..gt] == p, (gt..hi] > p
partition3By func [T] (a *[]T, lo uint, hi uint) Range = {
  mid uint = lo + (hi - lo) / 2
  if a[mid] < a[lo]  then a.swap(mid, lo)
  if a[hi]  < a[lo]  then a.swap(hi, lo)
  if a[hi]  < a[mid] then a.swap(hi, mid)
  a.swap(mid, hi)                       // median at hi; a[lo] <= pivot
  pivot const *T = &a[hi]               // view-pinned: no copy, no move
  lt uint = lo
  gt uint = hi - 1                      // the scan is (lo..hi): a[hi] never moves
  i uint = lo
  loop i <= gt {
    if a[i] == pivot then
      i += 1                            // == : the middle, left alone
    else if a[i] < pivot then {
      a.swap(lt, i)                     // < : into the lt region (self-swap elided)
      lt += 1
      i += 1
    } else {
      a.swap(i, gt)                     // > : into the gt region (self-swap elided)
      gt -= 1                           // i stays: re-examine the swapped-in value
    }
  }
  return Range { lt, gt }               // a[hi] (== pivot) sorts with the right
}                                       // subrange — it is just one more element

sortRangeBy func [T] (a *[]T, lo uint, hi uint) = {
  if hi <= lo then
    return
  r := a.partition3By(lo, hi)
  // guards keep lt - 1 / gt + 1 inside uint — no underflow on the ends
  if r.lt > lo then
    a.sortRangeBy(lo, r.lt - 1)
  if r.gt < hi then
    a.sortRangeBy(r.gt + 1, hi)
}

// sort in place; requires `infix_operator<` AND `infix_operator==` for T to
// be in scope. Together they must form a total order — for any a, b exactly
// one of a < b, a == b, a > b holds (an incomparable value, e.g. float NaN,
// falls to the "greater" side and clusters there). The `<` and `==` written
// above resolve to those operators at each instantiation.
quickSort func [T] (a *[]T) = {
  if a.length < 2 then
    return
  a.sortRangeBy(0, a.length - 1)
}
```

## Usage

`a.quickSort()` is method sugar for `quickSort(&a)` — the receiver (`[]int`) is
auto-addressed into the writable view parameter.

```c
a []int = {9, 3, 7, 1, 5}
a.quickSort()
loop e in a do
  out.println("%d{e}")     // 1, 3, 5, 7, 9
```

Operator overloading extends `quickSort` to any type with `infix_operator<`
and `infix_operator==` in scope:

```c
Point struct = {
  x int
  y int
}

infix_operator< func (a const *Point, b const *Point) bool = {
  if a.x != b.x then
    return a.x < b.x
  return a.y < b.y          // same x: order by y
}

infix_operator== func (a const *Point, b const *Point) bool = {
  a.x == b.x && a.y == b.y  // structural — agrees with the order
}

pts []Point = {Point {2, 9}, Point {1, 5}, Point {2, 1}}
pts.quickSort()
loop p in pts do
  out.println("(%d{p.x}, %d{p.y})")   // (1, 5), (2, 1), (2, 9)
```

Adversarial orderings — descending, by field — are orderings of the type, so
they live in the type: wrap the element and give the wrapper its own
operators. There is one sort, and it is always the in-scope operators' sort.

```c
// descending floats: a wrapper whose < reverses the ordering
Desc struct = { v float }
infix_operator< func (a const *Desc, b const *Desc) bool = {
  b.v < a.v               // reversed: greater v sorts first
}
infix_operator== func (a const *Desc, b const *Desc) bool = {
  a.v == b.v              // equality is unchanged
}

ds []Desc = {Desc {3.5}, Desc {1.0}, Desc {2.25}}
ds.quickSort()
loop d in ds do
  out.println("%f{d.v}")    // 3.5, 2.25, 1.0
```

## Notes

1. **Heap elements sort in place by moves — no Copy required.** `swap` takes
   through a `t T` temporary: for Copyable elements that is a copy; for
   strings and other heap types it is a **move**. Moving a value out of a
   slot is *defined* — reading a non-Copy element leaves the slot
   **uninitialized**, and the two assignments in the body are **move-in
   reinitializations** (the slot-take rule: `OWNERSHIP_RULES.md` §6;
   LINKED_LIST note 11). The checker verifies every
   slot is reinitialized before the array escapes the function, so no
   observable empty slot ever exists. All moves re-home backing within the
   caller's statement-block arena, so nothing allocates and nothing copies
   (note 5). `const *T` operators already make *comparing* heap elements
   cheap (shared views, auto-borrowed). Two more ownership rules live here:
   the **pivot is a const view into its own slot** (`pivot const *T = &a[hi]`)
   — compared, never copied or moved, so the partition transfers no element
   at all — and **`swap(a, i, i)` is identity**, the checker eliding the move
   trio that would otherwise read back the slot it just vacated.

2. **The ordering is the type's operators.** `partition3By` and `sortRangeBy`
   take no comparators: they write `<` and `==` literally, and the compiler
   resolves `infix_operator<` / `infix_operator==` for `T` at each
   instantiation (intrinsic for `int`/`float`, user-defined for `Point`).
   Together the operators must form a total order. A different order is a
   different type: wrap the element (the `Desc` example) and give the
   wrapper its own operators — one sort, one place the ordering lives.

3. **Three-way partition kills the all-equal and pre-sorted degeneracies.**
   Median-of-three keeps sorted and reverse-sorted input at O(n log n); the
   middle (== pivot) region makes *all elements equal* a single pass — no
   `<` or `>` branch ever fires, `i` walks the array once, and both
   recursions are empty: O(n). Heavy duplication is grouped in one partition
   instead of being dribbled out one element per pass. One artifact to note:
   the pinned pivot sits at `a[hi]`, *inside* the right subrange, and is
   sorted there — it is just one more element equal to the middle.

4. **`uint` underflow is designed out.** `quickSort` bails when
   `length < 2`, so `a.length - 1` never wraps; `partition3By` is only
   called with `hi > lo`, so `gt = hi - 1` stays in range; the recursive
   calls are guarded by `r.lt > lo` and `r.gt < hi`, so `lt - 1` / `gt + 1`
   stay in range. Inside the scan, the median arrangement guarantees
   `a[lo] <= pivot`, so the `>` branch can never fire at `i == lo` and `gt`
   never descends below `lo` — a consequence of the operators' totality.

5. **In-place, no allocations.** The array is mutated through a caller-owned
   view; no arena growth — heap-element swaps re-home backing within the
   caller's statement-block arena instead of allocating (note 1). Recursion
   is plain stack (average depth ~log2 n), and no block-arena work happens at
   any level.

6. **Trailing-lambda names are local.** A single-parameter lambda binds its
   argument as `it` (reserved inside the body): `{ it.value * 2 }`. Several
   parameters declare their own names before `:`: `{ x, y : x < y }`. Nothing
   is inherited from the callee — function types carry no parameter names.

7. **Operators take `const *T` — non-owning and universal.** `==` and `<` must
   never take ownership, so `&T` is out (that moves in). Value params
   (`const T`) would compare Copyable types without ownership, but a struct
   holding arrays or strings is not Copy — `const T` is a compile error for
   it — so the value convention would silently forbid operators on heap types.
   Settled convention, here and in `LINKED_LIST.md`:
   `infix_operator< func (a const *Point, b const *Point) bool`. Operand values
   auto-borrow into the const views (see README, "Copyable types"); comparing
   costs no copies and works for every type.

8. **Not stable.** Equal keys are permuted freely. If relative order of
   equals matters, a stable sort (e.g. merge sort, to be written) is the fix.