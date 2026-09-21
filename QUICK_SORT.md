# Quick Sort

An in-place, generic quick sort, written only with what the language already
has: `&`-method sugar on writable array views, generic functions, the
`loop j in lo..<hi` range form, and **scope-based operator overloading** — the
ordering for `quickSort` comes from `infix_operator<` being in scope for the
element type, exactly like `infix_operator==` powers `find` in the linked
list.

## The sort

```c
// Swap two slots of an array. `t` holds a copy, so T must be Copy.
swap : func [T] (a : *[]T, i : uint, j : uint) = {
  t : T = a[i]
  a[i] = a[j]
  a[j] = t
}

// Lomuto partition. The pivot is the median of {a[lo], a[mid], a[hi]},
// which keeps sorted and reverse-sorted input at O(n log n) with no RNG.
// Returns the pivot's final slot.
partitionBy : func [T] (a : *[]T, lo : uint, hi : uint,
                        less : func (x : const T, y : const T) bool) uint = {
  mid : uint = lo + (hi - lo) / 2
  // arrange a[lo] <= a[mid] <= a[hi], then move the median to hi
  if a[mid].less(a[lo]) then a.swap(mid, lo)
  if a[hi].less(a[lo]) then a.swap(hi, lo)
  if a[hi].less(a[mid]) then a.swap(hi, mid)
  a.swap(mid, hi)          // pivot (the median) now sits at hi
  pivot : T = a[hi]
  i : uint = lo
  loop j in lo..<hi {
    if a[j].less(pivot) {
      a.swap(i, j)
      i += 1
    }
  }
  a.swap(i, hi)            // move the pivot into place
  return i
}

sortRangeBy : func [T] (a : *[]T, lo : uint, hi : uint,
                        less : func (x : const T, y : const T) bool) = {
  if hi <= lo then
    return
  p : uint = a.partitionBy(lo, hi, less)
  // guards keep p - 1 / p + 1 inside uint — no underflow on the ends
  if p > lo then
    a.sortRangeBy(lo, p - 1, less)
  if p < hi then
    a.sortRangeBy(p + 1, hi, less)
}

// sort in place; requires `infix_operator<` for T to be in scope
quickSort : func [T] (a : *[]T) = {
  if a.length < 2 then
    return
  a.sortRangeBy(0, a.length - 1) { x < y }   // x, y named after `less`
}

// sort in place by an explicit ordering
sortBy : func [T] (a : *[]T, less : func (x : const T, y : const T) bool) = {
  if a.length < 2 then
    return
  a.sortRangeBy(0, a.length - 1, less)
}
```

The intent of `quickSort` in full:

```c
quickSort : func [T] (a : *[]T) = {
  if a.length < 2 then
    return
  a.sortRangeBy(0, a.length - 1, func (x : const T, y : const T) bool {
    x < y
  })
}
```

## Usage

`a.quickSort()` is method sugar for `quickSort(&a)` — the receiver (`[]int`) is
auto-addressed into the writable view parameter.

```c
a : []int = {9, 3, 7, 1, 5}
a.quickSort()
loop e in a do
  out.println("%d{e}")     // 1, 3, 5, 7, 9
```

Operator overloading extends `quickSort` to any type with `infix_operator<`
in scope:

```c
Point : struct = {
  x : int
  y : int
}

infix_operator< : func (a : const Point, b : const Point) bool = {
  if a.x != b.x then
    return a.x < b.x
  return a.y < b.y          // same x: order by y
}

pts : []Point = {Point {2, 9}, Point {1, 5}, Point {2, 1}}
pts.quickSort()
loop p in pts do
  out.println("(%d{p.x}, %d{p.y})")   // (1, 5), (2, 1), (2, 9)
```

`sortBy` pins the ordering at the call site instead of the type system:

```c
f : []float = {3.5, 1.0, 2.25}
f.sortBy { x > y }        // descending — intrinsic `>` on floats
loop v in f do
  out.println("%f{v}")    // 3.5, 2.25, 1.0
```

## Notes

1. **Elements must be Copy.** `swap` copies through `t : T`, so strings and
   other heap types cannot be sorted in place today. The root cause is a spec
   gap: **moving a value out of an array slot is not defined** (a slot cannot
   hold "nothing" — there are no nulls). Until move-out exists, heap elements
   need a permutation-based sort (sort `[]uint` of indices, then reorder).
   `const T` parameters already make *comparing* heap elements cheap (shared
   handles) — only the swap is missing.

2. **The ordering is scope-based.** `quickSort` resolves `infix_operator<` for
   `T` where it is used (intrinsic for `int`/`float`, user-defined for
   `Point`). `sortBy` needs nothing from the type — `func (x : const T, y :
   const T) bool` is passed explicitly, so adversarial orderings (descending,
   by field) cost no operator definitions.

3. **Median-of-three handles pre-sorted data** without randomness. The one
   degenerate input it does not fix is *all elements equal* — every partition
   returns `lo` and the recursion degenerates to O(n²). The standard fix is a
   three-way partition (Dijkstra), TODO.

4. **`uint` underflow is designed out.** `quickSort`/`sortBy` bail when
   `length < 2`, so `a.length - 1` never wraps; the recursive calls are
   guarded by `p > lo` and `p < hi`, so `p - 1` / `p + 1` stay in range.

5. **In-place, no allocations.** The array is mutated through a caller-owned
   view; no arena growth, no moves. Recursion is plain stack (average depth
   ~log2 n), and no block-arena work happens at any level.

6. **Trailing-lambda names come from the declaration.** `{ x < y }` binds `x`
   and `y` because `less` is declared as `func (x : const T, y : const T)
   bool` — same mechanical rule as `{ v.value * 2 }` in the linked list.

7. **A convention nit to settle (cross-file).** Here and in the README,
   operators take value params (`infix_operator< : func (a : const Point, b :
   const Point) bool`, matching the README's `const string` string operator),
   but `LINKED_LIST.md`'s `infix_operator==` still takes `const *Point`.
   One convention should win — value params are the README-consistent choice.

8. **Not stable.** Equal keys are permuted freely. If relative order of
   equals matters, a stable sort (e.g. merge sort, to be written) is the fix.
