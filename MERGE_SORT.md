# Merge Sort

A stable, generic merge sort — the sort of equal keys: their relative order
is preserved. Written only with what the language already has: `&`-method
sugar on writable array views, generic functions, the `loop` forms, and the
`+=` append family. The ordering comes from the element type's
`infix_operator<` being in scope — and only `<`: the stable merge test is
*take the left element unless the right is strictly smaller*, so `==` is
never consulted (quickSort, by contrast, dispatches on both `<` and `==` —
see `QUICK_SORT.md`, ruling C13).

The sketch's one established mechanism is the **element move-append**
`buf += a[i]` on an owned `[]T`: the value is *moved* into a freshly grown
arena slot — the `string +` / `bytes +=` precedent (growth is already the
single sanctioned invalidation point for views into owned buffers,
`OWNERSHIP_RULES.md` §5), and no view is held across it.

## The sort

Bottom-up merge sort. Each level doubles the run width; each merge stages the
*sorted left run* out of `a` into a fresh scratch buffer by move-appends,
then merges the two runs back into `a`'s vacated slots — stably, `<` only.

```c
// Stage the sorted left run (a[lo..=mid]) into a fresh scratch buffer by
// moves, then merge it with the sorted right run (a[mid+1..=hi]) back into
// a[lo..=hi]. Stability: take the LEFT element unless the right is strictly
// smaller — equal keys keep original order without ever consulting `==`.
mergeBy func [T] (a *[]T, lo uint, mid uint, hi uint) = {
  buf []T = {}                    // owned scratch: arena buffer, grown by appends
  loop i := lo; i <= mid {
    buf += a[i]                   // move-append: a[i] vacated (slot-take), buf grows
    i += 1
  }
  i := lo                         // merge buf (left) and a[mid+1..=hi] (right)
  l uint = 0                      // back into a[lo..=hi]
  r := mid + 1
  loop l < buf.length && r <= hi {
    if a[r] < buf[l] then {       // right strictly smaller? move it
      a[i] = a[r]
      r += 1
    } else {                      // equal or left smaller — left wins (stable)
      a[i] = buf[l]
      l += 1
    }
    i += 1
  }
  loop l < buf.length {           // left tail: real moves into the vacated region
    a[i] = buf[l]
    l += 1
    i += 1
  }
  // the right tail, if any, is already in its final position — its slots were
  // never vacated, so nothing moves (see note 5)
}

// Stable sort in place. Requires `infix_operator<` for T in scope — the
// totality contract of C13; `==` is not consulted by the merge.
mergeSort func [T] (a *[]T) = {
  if a.length < 2 then
    return
  n := a.length
  width uint = 1
  loop width < n {
    {                             // one level, one arena (C7): every scratch
      lo uint = 0                 // buffer this level dies at this block's exit —
      loop lo < n {               // peak live O(n), see note 3
        mid := lo + width - 1     // right run empty? the pair is one run —
        if mid >= n - 1 then      // already sorted, nothing to do
          break
        hi := lo + 2 * width - 1
        if hi > n - 1 then
          hi = n - 1
        a.mergeBy(lo, mid, hi)
        lo += 2 * width
      }
    }
    width *= 2
  }
}
```

## Usage

`a.mergeSort()` is method sugar for `mergeSort(&a)` — the receiver (`[]int`)
is auto-addressed into the writable view parameter.

```c
a []int = {9, 3, 7, 1, 5, 3}
a.mergeSort()
loop e in a do
  out.println("%d{e}")     // 1, 3, 3, 5, 7, 9
```

Operator overloading extends `mergeSort` to any type with
`infix_operator<` in scope. Only `<` is needed — the `Point` from
`QUICK_SORT.md` sorts here without its `infix_operator==` (see note 8).

The point of a stable sort is the stability. Ties keep their original order:

```c
Item struct = {
  key int
  seq uint          // the original index — what stability must preserve
}

infix_operator< func (a const *Item, b const *Item) bool = {
  a.key < b.key
}

items []Item = {
  Item {4, 0}, Item {2, 1}, Item {4, 2}, Item {1, 3}, Item {2, 4}
}
items.mergeSort()
loop e in items do
  out.println("(%d{e.key}, %d{e.seq})")
// (1, 3), (2, 1), (2, 4), (4, 0), (4, 2)
// the seq column stays ascending among equals — a quick sort would permute it
```

## Notes

1. **Stability with `<` only.** The merge's test is *take the left element
   unless the right is strictly smaller* — `if a[r] < buf[l] then right else
   left`. Equal keys therefore keep original order without any `==`
   consultation. Contrast with `QUICK_SORT.md`'s three-way dispatch, which
   needs `==` first (C13): the two sorts sit on opposite halves of the
   ordering contract.

2. **The workspace is a move-append buffer — that is the mechanism.** An
   owned `[]T` grows by `buf += a[i]`: the element is *moved* into a freshly
   grown arena slot (`string +` / `bytes +=` precedent; growth already the
   single sanctioned invalidation point for views into owned buffers —
   `OWNERSHIP_RULES.md` §5 — and the staging loop holds no views, so nothing
   dangles). Appends are moves, never copies: heap element types need no
   Copy, exactly as elsewhere. `buf` is a fresh local per merge and never
   escapes, so its slots dying vacated (every element moved into `a`) is
   fine — repair-before-escape applies to containers that *leave* the
   function, not to dead locals (note 4).

3. **Arena accounting.** A function body is not an arena (C7): each `buf`
   lands in the nearest enclosing *statement-block* arena and is bulk-freed
   at that block's exit — here, the explicit per-level `{ … }`, so one
   level's worth of scratch (≈ n/2 slots) dies together at the level
   boundary: peak arena across the sort is **O(n) live**, O(n log n) churn,
   freed per level. (Loop bodies are code blocks too, so the same discipline
   would hold per iteration even without the block.) `QUICK_SORT.md`
   allocates nothing (note 5 there) — the workspace is the price of
   stability.

4. **Slot-take choreography.** Staging moves the left run out of `a`: those
   `mid - lo + 1` slots become uninitialized (C9 slot-take). The merge then
   reinitializes *every* vacated slot by a single move-in — each moved
   element is written exactly once, right-tail elements are written zero
   times (they never leave their slots), and the interleaved right moves
   vacate-then-refill as the write head advances. `a` escapes fully
   repaired; `buf` dies vacated but never escapes — repair-before-escape
   governs containers that leave the function, not dead locals.

5. **The right tail never moves.** When the left run is exhausted, the write
   head `i` sits exactly on the right run's current head `r` (both advanced
   in lockstep past every consumed element), so the remaining right elements
   are already in their final slots. Writing them would be a chain of
   self-moves, which the C13 identity rule would elide anyway — the code just
   doesn't write them.

6. **`uint` guards.** `n < 2` returns before any arithmetic; `width < n` and
   `lo < n` keep the level bounds `lo + width - 1` and `lo + 2*width - 1`
   far from wrap for realistic sizes, and the `mid >= n - 1` break exits
   before `hi` is computed for a run-only pair; `hi` is clamped to `n - 1`
   afterwards. Width doubles each level and stops at `width >= n`, so the
   loop runs ⌈log2 n⌉ levels.

7. **No recursion.** Bottom-up merges iterate levels, so a top-down
   formulation's call-stack depth (⌈log2 n⌉) becomes a flat loop — the array
   is the only thing on the stack.

8. **`==` is not required — totality still is.** Only `infix_operator<` is
   consulted, so an ordering that defines just `<` (the `Item` example) is
   enough, unlike quickSort's `==`+`<` requirement. The C13 totality contract
   still applies: `<` must order every pair. An incomparable value (float
   NaN) makes the merge's "not (right < left) ⇒ take left" assumption
   wrong — the same degenerate input quickSort clusters.