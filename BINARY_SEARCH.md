# Binary Search

A search companion for the two sorts (`QUICK_SORT.md`, `MERGE_SORT.md`):
find a key in a sorted array, or the range it would occupy. Written only with
what the language already has: method sugar on a **const array view**,
`T?` for absence, and the element type's `infix_operator<` in scope —
and only `<`: equality is tested as "less in neither direction", so the
sketch never consults `==` (the `MERGE_SORT.md` half of the C13 ordering
contract).

Absence is a **data condition**, not a failure — `uint?` and never a `T!`,
the `getAt` convention of `LINKED_LIST.md`.

## The search

`lowerBound` is the primitive: the first index at which the key could be
inserted without breaking the sorted order — equivalently the count of
elements strictly below the key. `search` then needs exactly **one** extra
comparison: `lowerBound` already guarantees the element it returns is
not `< key`, so the key is present iff that element is not `> key` either.

Half-open bounds `[lo, hi)` keep the arithmetic uint-safe: `hi` only ever
moves down to `mid` (never `mid - 1`, which could underflow at 0), `lo` only
up to `mid + 1 <= hi`, and `mid := lo + (hi - lo) / 2` stays strictly inside
the bounds — never wrapping, always shrinking.

```c
// first i in [lo, hi) with NOT (a[i] < key) — the insertion point, and on
// the whole array the count of elements strictly below key. Needs only
// `infix_operator<` for T in scope; the C13 totality contract applies.
lowerBound func [T] (a const *[]T, key const *T) uint = {
  lo uint = 0
  hi := a.length               // half-open [lo, hi) — hi = mid, never mid - 1
  loop lo < hi {
    mid := lo + (hi - lo) / 2
    if a[mid] < key then
      lo = mid + 1
    else
      hi = mid
  }
  return lo
}

// an index i with NOT (a[i] < key) AND NOT (key < a[i]) — "equal under `<`" —
// or absent. lowerBound's contract already supplies the first half, so the key
// is present iff the element at the insertion point is not `> key` either.
search func [T] (a const *[]T, key const *T) uint? = {
  i := a.lowerBound(key)
  if i < a.length && !(key < a[i]) then
    return i                  // auto-wrap: success
  return                      // absence (bare return)
}
```

## Usage

`a.search(3)` is method sugar for `search(&a, 3)`; the receiver is
auto-addressed into the **const** view parameter, and the key value
auto-borrows into its `const *T` parameter — the operator convention of the
sorts, so heap element types cost nothing.

```c
a []int = {9, 3, 7, 1, 5, 3}
a.mergeSort()                          // 1, 3, 3, 5, 7, 9
if i := a.search(3)? then
  out.println("3 at %d{i}")            // 1 — the FIRST 3 (see note 5)
else
  out.println("3 absent")
n uint = a.lowerBound(6)               // 4 — count of elements < 6: {1, 3, 3, 5}
```

The two-sided range query — every element in `[k1, k2)` — is the pair of
bounds:

```c
lo := a.lowerBound(3)                  // 1 — first element >= 3
hi := a.lowerBound(7)                  // 4 — first element >= 7
loop i := lo; i < hi {                 // the keys in [3, 7): 3, 3, 5
  out.println("%d{a[i]}")
  i += 1
}
```

`search` inherits the sorts' ordering story: a type needs only
`infix_operator<`, and a *query value* only the ordering fields. The `Item`
from `MERGE_SORT.md` — keyed, stable-sorted — searches by a partial key
without `==`:

```c
if i := items.search(Item {4, 999})? then     // `<` on Item consults only the key
  out.println("first 4 at %d{i}")             // 3 — (4, 0) in the sorted run
else
  out.println("no 4s")
```

## Notes

1. **`<` only.** Equality is the pair of negated comparisons — `!(a[i] < key)
   && !(key < a[i])`. `search` runs just the second one because `lowerBound`
   already returns an element that is not `< key`. The C13 totality contract
   stands: `<` must order every pair. The degenerate incomparable input is
   NaN as before — an array containing a NaN makes both directions false, so
   `search` would "find" it at a slot where `lowerBound` lands; the sorted
   precondition is the caller's obligation, exactly as in the sorts.

2. **Absence is data.** `uint?` — a bare `return` spells "not present" —
   never a `T!`, never a panic. There is no failure mode: the halves are
   proven disjoint and exhaustive (note 3), so the only outcome is an index
   or a well-typed nothing.

3. **uint-safety by construction.** Over a sorted array of length `n`, every
   step shrinks the gap `hi - lo`: when `a[mid] < key`, `lo` moves to
   `mid + 1 > mid`; otherwise `hi` moves to `mid < hi` (both follow from
   `lo < hi`, which guarantees `lo <= mid < hi`). Termination is the gap
   reaching 0, at most ⌈log2 n⌉ + 1 steps, so no bound is ever computed
   outside `[0, n]` and nothing underflows or wraps.

4. **Const reading.** `lowerBound` and `search` take `a const *[]T` and never
   mutate — the search is read-only, no slot-take, no views escape, and no
   arena work happens at all. The range-query loop above reads through the
   same const view; auto-dereference covers the element reads.

5. **The first equal, thanks to the stable sort.** `search` returns the
   *leftmost* equal element — exactly the insertion point `lowerBound`
   anchored. Combined with `MERGE_SORT.md`'s stability (equal keys keep
   original order), the found slot is the earliest survivor: for the `Item`
   sequence, `search(Item {4, 999})` lands on index 3 — the `(4, 0)` slot,
   never the later `(4, 2)`. That claim is the stable sort's property, not
   the search's: with a quickSort'd array, equal keys have no prescribed
   order beyond the first-equal guarantee.

6. **Partial keys come free.** Only `<` decides, so a query value needs only
   the ordering fields — `Item {4, 999}` searches on key 4 while its `seq`
   is never compared; `999` just completes the type. This mirrors the sorts
   (note 1 of `QUICK_SORT.md`): the ordering is the type's operators, and
   equality is "less in neither direction" against that same `<`.

7. **Cost and applicability.** Each probe is one `<` comparison; the search
   is read-only and allocates nothing (note 4), so it composes anywhere a
   const view is in hand. It needs a sorted array — `MERGE_SORT.md`, or any
   order-respecting builder — and that sortedness is the caller's
   obligation, like the totality contract itself.