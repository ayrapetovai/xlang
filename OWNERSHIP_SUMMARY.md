# Ownership rules — theses

One thesis per rule, distilled from `OWNERSHIP_RULES.md` (normative; the core
wins on conflict). C-rulings are folded in where they amend a rule.

## O — Original rules (condensed)

1. `defer` runs at the end of the lifetime where it was defined.
2. Memory deallocation is implicit — no user-visible free.
3. Destruction is explicit: `dispose` at user-initiated ends, else a compile
   error; runtime ends (cell free, container/arena drop, overwrite-after-take)
   drop by shape (C30).
4. `dispose` carries the deallocation machinery for its parameter.
5. `dispose`'s first argument comes by ownership transfer.
6. An owned value is passed on, destroyed, or moved out at its end — never
   silently.
7. Ownership transfer copies nothing: by reference, or by value for
   integrals.
8. The compiler builds the deallocation machinery inside a defined
   `dispose`.
9. A `dispose` without move semantics is a compile error.
10. After transfer (block, spawn, function), the source binding is dead.
11. A view transfers a value without transferring ownership.
12. A non-owner may not destroy a value.
13. No partial consuming: consuming a field consumes the whole struct.
14. A mutable view passes by reference, only into a lifetime contained in
    the owner's.
15. Views never cross coroutines; `Mutex` / `Atomic` / `chan` cross as
    refcounted handles.
16. An owned return cannot land in a non-owning binding.

## Lifetimes, destruction, transfers

- Lifetimes: block scope, function body, expression — plus each loop
  iteration (arena + defer lifetime).
- A disposable discharges exactly once — dispose, defer, or move-out — and
  the discharge must dominate every path from the last owned use to the
  lifetime end; field disposes run in declaration order; no inference, no
  alias analysis; `T?` payloads exempt (C30).
- No `dispose` ⇒ scope-end deallocation: arena bulk-free, or refcount
  decrement for handles.
- A disposable temporary must be moved out or bound before its expression
  ends — in-place drop is a compile error (C28).
- `defer` fires LIFO before arena teardown; in a loop body, per iteration
  (C30); deferred bodies must be infallible.
- `panic` aborts, skipping defers — an intrinsic, never user-spelled;
  failure is `T?` / `T!`.
- Closures own their captures; an abandoned registration still drops owned
  disposable captures by shape (C30).
- Copyable arguments copy in; everything else moves; `&T` is the explicit
  transfer form.
- A lender still owes; a mover owes nothing — the gate keys on the last
  owned use.

## Views, functions, operators

- No lifetime inference: views are syntactic containment, so a mutable view
  goes only into *called* functions.
- A spawn is a different lifetime: only ownership crosses — never a view;
  sync handles cross by sharing, and sharing increments the count (C30).
- A view can never dispose or be moved out of; constness widens, never
  narrows.
- Returning a view of a local or re-borrowing a consumed binding is a
  compile error.
- Arguments are named; a trailing lambda (≤ 2 params) is allowed; function
  types carry no parameter names.
- Operators are functions over `const *T`: non-owning, operands
  auto-borrowed, never moved.
- `clib` borrows are `const *T` and call-live; retention is a crossing site
  (move or pool-promotion, never a view). "C must not retain" stays a
  contract — the one carve-out from "all safety is compiler-enforced"
  (C28/C30).

## Arenas and slots

- A function body is not an arena: callee allocations land in the caller's
  statement-block arena; owned locals still deallocate at function end (C7).
- Buffers grow by element move-append; growth re-points views, never
  invalidates them (C28).
- Each loop iteration is an arena; surviving values move out with backing
  relocated (C28).
- A store into an outer binding allocates in that binding's arena —
  loop-carried accumulators are safe and bounded (C30).
- Constants intern by value into a module-global pool, freed on module
  unload; no GC — cycles are impossible by construction.
- Slot-take leaves a tracked uninitialized slot; reads are compile errors
  until a move-in reinitializes it; slots must be repaired before the
  container leaves the function — a container that dies in place may keep
  them (C9).
- `take` on an unlinked node stays dead; `swap(a, i, i)` is identity (C13).
- Declarations default to real values — scalar 0, empty slice, `T?` absent
  (absence, never a vacated slot).
- Overwriting a disposable occupant is a compile error unless taken out
  first (C28); copying a heap value by value is a compile error.

## Shared cells

- `Mutex[T]`, `chan[T]`, `Atomic[T]` are refcounted handles to synchronized
  runtime cells — the only shared mutable state; `Atomic[T]` takes a
  lock-free scalar, checked per instantiation.
- At zero refs the cell frees and the payload runs its drop by shape (C28).
- Cell payloads cannot contain handles; arena-hosted handles decrement via
  the container's drop (C28).
- Any holder may close a channel; double-close aborts; close is sanctioned
  cancellation — a parked receive on a closed-and-drained channel yields
  absence (C28).
- Sends move owned values; const handles share, and sharing increments (C30).
- A receive owns: the unwrapped payload faces the full gate — `_ = fd?` is
  a compile error — while the `T?` layer is exempt (C30).

## Errors and reflection

- Errors are an intrinsic kind with non-disposable payloads; failure is
  `T!`, absence is `T?` — both unstackable and unmatchable (C10).
- `return v` wraps automatically; `x = {}` clears `T?`; `T?` payloads are
  gate-exempt, so clearing one holding a disposable leaks it (documented) —
  only the unwrap consumes and carries the obligation (C30).
- Reads are obligated-unwrap: `?` / `?? default` / checked `if/loop …?` / the
  absence tests `== {}`, `!= {}` for `T?`; `!` / `try` for `T!`. A bare read
  as `T` is a compile error.
- `try` wraps a statement or expression, never a block (C21); one flat
  `catch`; handlers are checkable, not exhaustive. Inside a guarded scope a
  bare `!` fails the region to its own `catch` (not an early return), a bare
  `?` is a compile error there, `?? default` stays legal, and `!`/`?` are
  mutually exclusive.
- Error payloads read only through a kind-bound name (an `is` binding);
  reading through unbound `e` is a compile error.
- `e is K k` deep-first-match binds, never narrows; bound names are
  view-only; `causes []error` is opaque; `==` on errors is a compile error
  (C11).
- `loop i, x in ar` carries a uniform 0-based ordinal.
- Reflection is read-only by shape; codec `O` must be wire-shaped (no
  `*T` / `any` / disposable) so cycles are impossible; `toJson`/`toBytes`
  fail with value errors; `fromJson`/`fromBytes` build the graph in the
  caller's arena and bounds-check every intrinsic read (C12/C17).

## Ordering and mechanics

- Ordering is the type's in-scope `<` and `==`, resolved per instantiation,
  forming a total order; dispatch is `==` → `<` → else `>` (C13).
- The pivot is view-pinned, never moved; a different order is a different
  type — wrap it (C13).
- Keys equal under in-scope `==` must hash equal — the one unprovable
  contract (C16).
- `loop`, not `for`; assignment is a statement (no `++`/`--`); `match`
  exhaustively consumes and never covers `T?`/`T!`.
- `panic(...)` is a compile error — abort is runtime-only (C20).
- Modules: prefix declarations; globals packed into `module_initializer`;
  module unload drops owned disposable globals by shape; direct-import
  visibility only; `#compiler.private` hides (C22/C30).
- Output aborts on write failure; the non-panicking surface is `runtime.log`.
- Unwrap of an owned payload is a consume; a view-unwrap binds a const view.
- The checker is morphological — no lifetime inference, no alias analysis;
  generics are checked per instantiation; casts are always fallible
  `X.from(y)` (C23).