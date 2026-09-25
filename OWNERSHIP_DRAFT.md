# Memory ownership — deliberation and resolution record

**Premise.** The normative rules are `OWNERSHIP_RULES.md` — the consolidated
core (the original `QUERY.txt` answers plus the interview rulings C1–C13);
the shipped sketches (README socket server, `LISTEN.md`, `QUICK_SORT.md`,
`LINKED_LIST.md`, the threads/channels sections, `## Bytes`, reflection) are
drafts and must conform to those rules. Where a draft contradicts a rule, the
draft is wrong. This file is the record of the deliberations: the numbered
rule renderings below (R1–R6, crossing-coroutine, C9+ missing rules), the
applied fixes, and the decision appendix.

---

## R1 — Lifetime and destruction

- Every owned value is **passed to another owner or destroyed** at the end of
  its lifetime. Lifetimes are exactly three: **code block scope**, **function
  body**, **expression (temporary value)**.
- Destruction is **implicit memory deallocation** plus, for types that define
  `dispose`, an **explicit `dispose` call**:
  - **type defines `dispose`** (disposable): the program calls `dispose` (or
    defers / moves the value out) — never the compiler — and the checker
    verifies exactly one discharge before the value dies. The compiler
    generates the **deallocation machinery inside the `dispose` body**, not
    at call sites: for a synthesized struct `dispose`, field disposes run in
    declaration order, then the value's own heap allocations are freed. This
    is the resolution of the Q1/Q3 wording tension: the *call* is explicit,
    the *machinery* is generated.
  - **type defines no `dispose`** (string, `[]T`, bytes, scalars, ordinary
    structs, shared-cell handles): the compiler generates the deallocation
    machinery at the **end of the variable's scope** — the implicit free of
    the value's own heap allocations (arena bulk-free; the refcount decrement
    for `chan`/`Mutex`/`Atomic` handles, per R6).

Consequence: a value that lent a view still owes its destruction; a value
that moved out owes nothing. The checker keys on the last owned use, not on
scope text.

## R2 — Ownership transfer mechanics

- Transferring ownership **copies nothing**: the value moves.
- The move is spelled:
  - **by reference** — `&T` parameter — the explicit, default form for
    pass-through transfers (`spawn echo(&conn)`, `dispose func (f &Fd)`,
    `foo(&s)`);
  - **by value** — a plain `T` parameter — copy-in for **Copyable** values
    (integral types like `int`/`bool`, pointers, Copyable structs/enums) and
    **move-in** for any non-Copyable value (Rust-style: `pushBack(v T)`
    stores `v`; `mutex.new(v T)` stores `v`). A non-Copyable argument binds
    by value only by moving; the caller's binding is consumed.
- **Returns** transfer ownership by value (the receiver side of the same
  move): `return Ok(buf)`, `return Some(first.value)`, `Result[Connection]`.
- A string literal passed to an owned `string` value parameter **materializes**
  a copy of the pool bytes into the current arena, then moves (pool values
  have no unique owner to move).

## R3 — dispose

- `dispose` **must take ownership**: a `dispose` declared without `&T`
  (move-in) semantics is a **compile error**.
- A **non-owner may not destroy**: calling `dispose` on a value you do not
  own is a **compile error**. A view binding has no ownership to give away,
  so release forms are unreachable from views (the README handle barrier).

## R4 — Consumption

- **Ownership transmission is a point of no return.** Once ownership passes
  to a block, a spawned function, or any function, the source value is
  unusable — any further use is use-after-consume, a compile error.
- **No partial consuming.** Consuming a field consumes the whole struct (a
  consume takes ownership and does not give it back). *Reading* a Copyable
  field is not a consume. Consequence: to let the caller keep the rest of a
  struct, do not move a field out of it — share it (`const`) or view it.

## R5 — Views and lifetimes (syntactic containment)

- An owner may give a **view** without giving ownership: `*T` (mutable),
  `const *T` (read-only), `&expr` (address-of). Values auto-borrow into
  `const *T` parameters.
- A **mutable view** may be passed only to functions whose lifetime is
  **syntactically contained** in the owner's — i.e. *called* functions, whose
  body is textually enclosed in the caller's scope. This is the Q9 rule,
  realized **without lifetime inference**: the containment check is
  morphological (compile-time, no borrow-checker).
  - a function that **calls** another may pass a mutable view;
  - a function that **spawns** a coroutine (a different lifetime) passing a
    mutable view is a **compile error** — only ownership transfer crosses;
  - returning a view of a local, or storing a view beyond the owner's scope,
    is a compile error.
- Views are thread-local by shape: any value whose shape contains a view
  (`*T`, `const *T`) never crosses a coroutine boundary.

## R6 — Shared cells: mutex, channel, atomic

**Exception to R5.** The intrinsic `Mutex[T]`, `chan[T]`, and `Atomic[T]`
handles are **refcounted pointers to runtime-managed cells**. Conceptually
each coroutine holding a handle owns a **mutable view to the cell,
implemented with synchronization under the hood of the language** — that is
what permits them to cross coroutine boundaries while every other shared
state must be `const` or owned.

- **Reference counting.** The handle crosses by sharing; copying a handle
  increments the count. The **deallocation machinery is embedded at the end
  of the lifetime in which each handle appeared**: each handle's scope-exit
  decrements; at zero the cell is freed (its payload destroyed per R1 —
  `dispose`d if disposable).
- **Channel close is not owner-only.** Every coroutine holding a channel
  handle co-owns the channel's mutable view, so **any holder may close it**:
  `ch.dispose()` is the close. The call still *consumes that holder's local
  binding* (R3/R4); a close on an already-closed channel **aborts at
  runtime** (double-close is runtime-checked, since the static checker cannot
  track who closes). A closed-but-referenced cell keeps draining until the
  last handle dies; sends to a closed channel abort.
- The static exactly-once gate of `## Resources` applies to *resource*
  handles (Fd, Connection, Listener, mutex guards); **channels are excused**
  from the static obligation — their close is a synchronized runtime
  operation on the shared cell. Guards stay statically gated (`g.dispose()`
  = unlock).

## Crossing a coroutine boundary (spawn args, `ch <- v`, escaping closures)

Morphological check at the site, per instantiation:

| Shape | Crossing rule |
|---|---|
| contains a view (`*T`, `const *T`) — iterators, `ListIterator`, `Locked`, `any` | **compile error** |
| owned / disposable (string, `[]T`, bytes, structs holding them, Connection, Listener) | **moves** — backing relocates to the receiving coroutine's arena; disposable obligation rides along |
| Copy (scalars, enum payloads, `type`) | copied |
| frozen (`const string`, `const []T`, pool literals) | promoted to the immortal module-global pool, then shared |
| **refcounted handles** (`chan`, `Mutex`, `Atomic` — mutable-view-with-sync) | shared via the cell (R6) |

---

# Conformity of the examples

| Rule | Status against the drafts |
|---|---|
| **R1** | ✓ Arena destruction + explicit `dispose`/`defer` everywhere (`conn.dispose()`, `g.dispose()`, `ch.dispose()`); non-disposable owned values get compiler-generated dealloc at scope end (the R1 remark); temp destruction is implicit — no draft change needed. |
| **R2** | `&T` move-ins ✓ (`spawn echo(&conn)`, `foo(&s)`, `dispose (f &Fd)`); value-param moves ✓ (`pushBack(v T)`, `mutex.new(v)`). Read-only params are `const string` or views throughout: `newListener`/`portFromEnv`/`readFile`/`getUserAuthorities`/`greet` → `const string`; `truncateRead`/`readRequest` → `*File`/`*Connection` views (applied Sep 24). |
| **R3** | ✓ `dispose func (f &Fd) = …` is `&T` move-in; the release path is unreachable from views (handle barrier). |
| **R4** | ✓ Whole-struct moves (`fd = fd`, `Connection { fd = fd, … }`) respect no-partial-consume. `newListener(cfg.address, cfg.port)` partial-consume hazard resolved by `const string` params; the `Connection { fd = fd, peer = socket.peerName(fd) }` read-after-move ordering fixed by hoisting `peer := socket.peerName(fd)` before the move (README + LISTEN — applied Sep 24). One carve-out: a move-in that *reinitializes* a slot left uninitialized by a move-out/`take` is the sanctioned repair, not a use-after-consume (C9). |
| **R5** | ✓ Views go only to *called* functions (`serve(&l)`, `quickSort` chain, `listener.accept()`); no view is ever spawned; `spawn echo(&conn)` is a move, not a view. |
| **R6** | ✓ `spawn pong(ch)`, `spawn worker(1, counter, ops)` cross by sharing; mutex guard stays local. README channels text now states co-ownership (any holder may close; double-close aborts at runtime; refcount frees the cell at the last handle's scope exit); atomics/mutexes/channels state the dealloc-at-lifetime-end machinery; channels excused from the static gate (applied Sep 24). |

# Missing rules now specified — IO

1. **Synchronous vs asynchronous tenure.** A synchronous syscall borrows its
   buffer for the call's duration. An asynchronous registration (epoll /
   kqueue / io_uring, completion callbacks) **retains the buffer past the
   call** — that is a crossing site: it must take **ownership** (move) or
   pool-promote, never a view (extends the README `clib` rule to async).
2. **Failure-path disposal.** A function returning `Error(e)`/`None` must
   have discharged every owned disposable it still holds first (open → read →
   fail ⇒ close before returning `Err`).
3. **Buffer growth invalidates outstanding views** — the README `bytes` rule
   generalizes to owned `string` and `[]T`: growth is the single sanctioned
   invalidation point for views into owned buffers.
4. **Receive arena.** Every receive / slurp allocates owned buffers in the
   receiving coroutine's arena; the OS never allocates into a caller arena.
5. **Non-blocking callbacks may not capture views** — a registered
   callback is another coroutine: owned moves, frozen shares, views CE.

# Missing rules now specified — bidirectional linked list (heap elements)

1. **`take()` / detach — unlinked-node form.** Extracting an owned payload
   from a node reached through a view: `take` **relocates the payload's
   backing into the caller's (current) arena** — the same machinery as
   channel receive. The node must be **unlinked first**; an unlinked node's
   location is unreachable, so `take` there simply **consumes the node** (dead
   afterwards; any later read is use-after-consume, R4). On a *live*
   container slot the same machinery instead leaves the slot uninitialized
   and reinit-able — rule 6 below.
2. **Move-in to slots.** Storing an owned value into a node field is a
   move-in (`pushBack(v)` by-value move; `node.value = v`). The list owns the
   value; the arena owns the memory; bulk-free covers both.
3. **No-empty-slot is solved by dead nodes, not empty slots.** After
   unlink+take the node is unreachable; the sentinel is exempt (never
   extracted). Live container slots admit a *transient* uninitialized state
   (rule 6) — unobservable, repaired by move-in before the container escapes.
4. **Disposable payloads.** `Head[T]`/`Node[T]` are disposable iff `T` is —
   the synthesized dispose is a **walk-and-dispose** over live nodes (or keep
   `*T` views of externally-owned resources per LINKED_LIST note 10).
5. **Iterator stability is a stated rule.** Arena nodes never move, so
   `ListIterator`/node views stay valid across pushes and pops; only taken
   nodes die. (This is a feature: std::list-style stable iterators.)
6. **Slot-take: moving out of a live container slot is defined.** Reading a
   non-Copy element into a local (`t := a[i]`) or `take`-ing an element of a
   live `[]T` **moves the element out and leaves the slot *uninitialized*** —
   a tracked non-value, not a null, and not an R4 consume. Reads of an
   uninitialized slot are compile errors until it is reinitialized (Rust:
   "prevents further reads until it is reinitialized"). Assignment `a[i] = v`
   into an uninitialized slot is a **move-in reinitialization** — the
   sanctioned repair, carved out of R4's point-of-no-return. A slot left
   uninitialized when its container moves or returns is a compile error (no
   *observable* empty slots). The checker tracks slot state linearly
   (initialized → taken → reinitialized) — morphological, no inference.
   Consequence: `quickSort`'s existing `swap` body (`t := a[i]; a[i] = a[j];
   a[j] = t`) is take + two reinitializations and sorts heap elements in
   place; the moves re-home backing within the caller's statement-block
   arena (C7), so nothing allocates. (Ruling C9.)

# Missing rules now specified — intrinsic errors (Result[T], C10)

1. **Error is an intrinsic kind.** `X error = { … }` declares a named error
   type with payload fields, syntactically a struct (`IOError error = {
   customParam int; customMessage string }`). Errors are values: created by
   the failing path, moved into the `Result` box on `Err`, bound by `catch e`,
   returned/moved out again — ordinary owned values.
2. **`Result[T]` has one parameter.** The error is implicit — the single
   intrinsic error type. The `E` parameter is gone, and with it the
   per-region uniform-E rule: any try region mixes failure origins freely;
   `!` returns the intrinsic error; `catch` binds it. (Unifies the spec's
   earlier conflicting `Result[string, error]` and single-arg `Result[...]`
   forms.)
3. **Payloads are non-disposable.** An error type declared with a field of a
   disposable type is a **compile error**. Errors never carry owned resources:
   the failure-path rule (dispose before returning `Err`) stays intact, and an
   *unread* error needs no discharge — just the R1 dealloc machinery at the
   end of its scope. Wrapping is allowed: an error may carry `cause error`
   (the Go `%w`-chain analog), and kind tests see through `cause` levels.
4. **Kind test is `is`, deep, binding a new name.** `e is IOError io` tests
   the error's **dynamic kind** — type identity — along the `cause` spine,
   outermost first, first match wins (Go's `errors.Is` walk). A true test
   binds the found member under the given name; `e` is untouched and its
   payload is statically unreadable until a name is bound. Bound names are
   **view-only**: `return io` is a compile error (R4 — a nested value cannot
   move out of its wrapper); chains are append-only. `==` between error
   values is a compile error — sentinels are payload-less kinds, tested with
   `is`. (Deep-`is` and binding rulings: C11 below.)
5. **Checkable, not exhaustive.** Error handlers are the deliberate Go-style
   relaxation of `match` exhaustiveness: a new error kind compiles everywhere
   and simply falls through until a test is added; the end of the handler is
   the implicit catch-all. (Answers the "this `e` must be any error"
   requirement — `catch e` binds the intrinsic error by construction.)

# Missing rules now specified — error-handling interview rulings (C11)

1. **`is` is deep.** Tests dynamic kind along the `cause` spine, outermost
   first; first match wins (Go's `errors.Is` walk).
2. **Model B binding.** `e is IOError io` is a pure boolean test; a true
   test binds the found member to the *given* name (`io`). `e` never
   changes referent — it stays the caught, top error.
3. **View-only binds, no extraction.** A bound name is a read-only view into
   the chain; `return io` is a compile error. Chains are append-only: the
   only transform is wrapping the top (`return SocketError { message = …,
   cause = e }`); re-contextualizing an inner error is impossible.
4. **Built-in `Error { message string, code int }` is public.** Intrinsics
   fill it from the OS (errno → `code`, message); user code may construct
   it; it is always a possible `cause`; fields read like any declared kind.
5. **Aggregates are opaque.** `causes []error` is legal payload (errors are
   non-disposable, so the slice is too) but `is` follows only the single
   `cause` spine; children are reached by explicit iteration.
6. **Loop `i` is the ordinal.** In `loop i, x in ar`, `i` is a 0-based
   iteration count the loop machinery maintains — always legal, uniform for
   every iterable; for arrays it coincides with the slot index.
7. **`==` on errors is a compile error.** Every "is this the error" question
   is answered by `is`; sentinels are payload-less kinds (`NotFound error =
   {}`), tested the same way.

# Missing rules now specified — reflection IO (C12)

1. **Reflection access is read-only by shape.** Every reflected value is a
   const view (heap shapes: string, enum, array, struct) or a Copy scalar;
   reflection never takes, moves, mutates, or disposes. Enforced with the
   ordinary mechanism: a serializer/visitor takes `const *O`, and consuming
   anything through a const view is a compile error — the walk is
   ownership-neutral by construction.
2. **Serialize keeps `Result`; failures are values, not types.** `toJson[O]
   (obj const *O, n := 0) Result[string]` — the only failure modes are a
   non-finite float and the depth cap, reported as a declared kind
   (`JsonWriteError { message, cause }`); deep failures propagate via `!` or
   wrap with `cause` for field context (C11). Output is arena-built in the
   caller's statement-block arena and dies with the caller's block (C7).
3. **JSON-shaped constraint, checked at instantiation.** `O`'s reachable
   field types must be scalars, string, enum, array, struct-of-those — no
   `*T`, `any`, or disposable fields. Serialization cannot cycle: no
   references exist to follow, so no pointer arm and no cycle machinery.
   Parse cannot construct refs from text, so the identical constraint
   governs `fromJson`.
4. **Parse owns nothing it returns.** `fromJson[O] (json const string)
   Result[*O]` — success **&-creates the whole O graph** in the caller's
   statement-block arena and returns a view into it: one bulk-free at the
   caller's block exit, no dispose (newList precedent, C7 — recursion lands
   every nested allocation in the outermost caller's arena). Failure is a
   declared kind with a byte offset (`JsonParseError { message, offset,
   cause }`), tested and bound under C11.

# Missing rules now specified — three-way partition (C13)

1. **The pivot is a view into its own slot, never a value.** The partition
   compares through `pivot const *T = &a[hi]`. A *value* pivot would MOVE a
   heap element out of `a[hi]` (C8/C5), leaving the slot uninitialized, and
   the scan reads `a[hi]` — a slot-take CE (C9). The median stays pinned in
   its slot for the whole scan and afterwards sorts with the right subrange.
   (Corrects the earlier Lomuto endgame, which read the vacated slot.)
2. **Ordering needs both `<` and `==`, written literally, not threaded.**
   The sort machinery takes **no comparator parameters**: `partition3By` /
   `sortRangeBy` / `quickSort` write `a[i] == pivot` and `a[i] < pivot`
   inline, and the compiler resolves `infix_operator<` /
   `infix_operator==` for `T` at each instantiation (scope-based; user
   ruling, Sep 24 — the comparator parameters are gone). Dispatch is `==`
   first, then `<`, else `>` — the pairing must form a **total order** (for
   any a, b exactly one of a < b, a == b, a > b holds), which is also what
   guarantees the `>` branch never fires at `i == lo` (so `gt` is
   uint-safe). Incomparable values (float NaN) fall to the `>` side and
   cluster there. An explicit or reversed ordering is expressed by wrapping
   the type (`Desc`-style struct) and giving the wrapper its own operators —
   one sort, one place the ordering lives.
3. **Self-swap is identity — the checker elides it.** `swap(a, i, i)` skips
   the move-out/move-in trio; performing it would read back the slot just
   vacated (a slot-take CE for heap elements) — a C9/R4 carve-out on top of
   move-in reinitialization. The old Lomuto sketch already contained one on
   its first iteration; the three-way makes the ruling unavoidable.
4. **Boundaries return by value, not by view.** `partition3By` returns a
   two-uint Copy struct — `Range { lt, gt }` — owning nothing, no views
   escaping the function; recursion re-derives the two subranges from it
   under the usual `uint` guards (`r.lt > lo`, `r.gt < hi`).

# Draft fixes applied (rules-first)

Applied per approval (Sep 24) across `README.md`, `LISTEN.md`, `QUICK_SORT.md`,
`LINKED_LIST.md` — pending your commit. Item 7 additionally touched
`LINKED_LIST.md` notes 4 and 10 (their "value T requires Copy" premises were
made false by value-params-move) and README's iterator *call sites*
(`begin(&ar)`, `next(it)`).

1. `newListener`, `portFromEnv`: param `address`/`name` → `const string`;
   call sites keep passing owned fields / literals (share, non-consuming).
   — README L1098, LISTEN L37
2. README `## Channels`: replaced the owner-close model ("only the owner
   closes / const carries no close obligation") with R6 co-ownership — any
   holder may close; double-close aborts at runtime; refcount frees the cell
   at the last handle's scope exit; static gate excused for channels.
3. README `## Threads and synchronization`: atomics join the refcounted
   exception; state the dealloc-at-lifetime-end machinery for all three.
4. README "Copyable types": value params **move** non-Copy values (fix
   "cannot pass by value" L559/L574 → "cannot *copy*"; `foo(s)` is a move if
   the param is `string`, a borrow only via const); add the `&` argument-
   position row (move-in iff the param is `&T`).
5. README checker list: add **no partial consuming** (Q8/R4) and the
   syntactic-containment rule for mutable views (R5).
6. Iterator protocol text + `Iterator struct { data T }` → `data *T`;
   containers iterate by view (as both real protocols already do).
7. `LINKED_LIST.md`: add heap support via `take()`; note 2's "combinators
   require Copy" relaxes under value-params-move (`map` over `Optional[string]`
   becomes legal).
8. `QUICK_SORT.md` note 1: "moving a value out of an array slot is not
   defined" — now answered by `take()` on the slot for heap elements.
9. `Connection { fd = fd, peer = socket.peerName(fd) }` (README + LISTEN):
   `fd` was moved into the field while `peerName(fd)` still read it — hoisted
   `peer := socket.peerName(fd)` into the match arm / success tail before the
   literal (R4).
10. `truncateRead` (README Bytes): `f File` owned, never discharged → `f *File`
    view; `f.readLine(&buffer)` writable-view spelling (R1). `readRequest`
    `conn &Connection` (read-only use) → `conn *Connection` view (R2).
11. README `## ! and ?` section: read-only `string` value params → `const string`
    (`readFile(path)`, `getUserAuthorities(login)`, `greet(login)`).
12. README `## Pointers` operator sketch: `infix_operator==` shown with
    `const *string` auto-borrowed operands, matching the `const *T` operator
    convention (auto-deref member access/indexing still reads cleanly).
13. README `## Channels`: `ch <- "hello, world"` comment now says the frozen
    pool buffer is *shared* into the cell, not moved (R6 consistency).
14. README "Where memory lives": function bodies are not arenas — callee
    `&`-creations and owned buffers land in the nearest enclosing
    statement-block arena (the caller's); owned locals still get per-variable
    deallocation at function end. Resolves the arena contradiction with
    `newList`/`pushBack`/`toJson` (ruling C7). LINKED_LIST note 7 restated
    from an assumption to a rule.
15. Slot-take rule (C9): moving an owned element out of a *live* container
    slot leaves it **uninitialized** — reads are compile errors until a
    move-in **reinitializes** it (the one R4 carve-out); uninitialized slots
    must be repaired before the container moves or returns. `take` on an
    unlinked node keeps dead-node semantics. Closes QUICK_SORT's in-place
    slot-swap edge — the existing `swap` body sorts heap elements unchanged.
    — OWNERSHIP_DRAFT missing rules #6, QUICK_SORT note 1 & note 5,
    LINKED_LIST note 11.
16. README integration of the remaining draft-only rules: new
    `### Slots: moving values out` (slot-take: uninitialized slots, reads CE,
    move-in reinitialization as the one R4 carve-out, no observable empty
    slots, per-slot checker state — incl. `take` spelled for the first time);
    checker list gains the two slot errors; `Where memory lives` gains the
    three-lifetimes (code block scope / function body / expression temporary)
    and literal-materialization (pool copy then move into owned `string`
    params) bullets; `Semantics that touch ownership` gains async-tenure
    (retention ⇒ move or pool-promotion, never a view) and callback-view
    rules; `Resources (dispose)` gains the failure-path-disposal rule
    (discharge before `Error`/`None` return). Closes the "integrated?"
    gap list (README only: container-level `take`/dead-node mechanics stay in
    the sketches).
17. Intrinsic errors + `Result[T]` (ruling C10): README `## Try / catch` and
    ``## `!` and `?` `` rewritten — `Result[T]` single-param everywhere
    (`truncateRead` `Result[string, error]` → `Result[string]`; the uniform-E
    rule dropped, replaced by the intrinsic one-error-kind model); error kinds
    declared `X error = {…}` with non-disposable payloads (checked at
    declaration); `e is IOError` kind test with compiler-verified narrowing
    (payloads readable only when narrowed); handlers explicitly
    checkable-not-exhaustive; `cause error` wrapping with `is`-through-cause
    semantics; checker gains the unnarrowed-payload-read and
    disposable-payload-declaration errors; Bytes `IOError` vocabulary
    cross-referenced as the first declared error kind. Modeled on Go's
    `error` interface + the `errors.Is`/`errors.As` distinction — grounded in
    the Go source (context7, Sep 24). — OWNERSHIP_DRAFT missing rules above.
18. Error-handling interview rulings (C11): `is` made explicitly deep
    (cause spine, top-first, first match wins); the committed C10 reading
    "e narrows" corrected to **binding** — `e is IOError io` binds the found
    member to a new name, `e` untouched; bound names declared **view-only**
    (no extraction; `return io` CE; chains append-only); built-in public
    `Error { message string, code int }` documented (intrinsics fill from
    errno; user-constructible; always a possible cause); `causes []error`
    aggregates declared **opaque** to the walk (explicit iteration only);
    `==` between error values made a compile error (sentinels = payload-less
    kinds, tested with `is`); loop pair-binding `loop i, x in ar` defined —
    `i` is the iteration ordinal, 0-based, uniform, machinery-maintained.
    — README `### Error kinds, `is`, and binding` rewritten (example now
    `if e is IOError io … %s{io.customMessage}`), checker list updated
    (payload reads need a kind-bound name; `==` on errors CE),
    `## loop with in` gains the ordinal rule; OWNERSHIP_DRAFT C10 rule #4
    corrected + missing-rules + decision C11.
19. Reflection IO under ownership (ruling C12): README `## Metaprogramming`
    rewritten — `toJson` corrected to `obj const *O` (reflection access is
    read-only by shape), keeps `Result[string]` with *value*-level failures
    only (non-finite float, depth cap → `JsonWriteError`); `O` constrained
    JSON-shaped at instantiation (no `*T`/`any`/disposable fields — cycles
    impossible by construction, so no pointer arm and no cycle machinery);
    array/struct commas use the loop ordinal + `.length` (the deleted
    first/last proposal's `!last` was dangling); new `fromJson[O] (json const
    string) Result[*O]` — &-creates the whole graph in the caller's arena,
    returns a view, failure = `JsonParseError { message, offset, cause }`
    (declared in `## Error kinds`, per C11). — README `### Error kinds`
    example aligned to the canonical `JsonParseError` shape
    (`%s{jp.customMessage}` → `%s{jp.message}`) + missing-rules + decision
    C12.
20. Three-way partition (ruling C13): QUICK_SORT.md `partitionBy` (Lomuto)
    replaced by Dijkstra three-way `partition3By` returning the two-uint Copy
    struct `Range { lt, gt }`; the median pivot is **view-pinned**
    (`pivot const *T = &a[hi]`) and left in its slot — a value pivot would
    MOVE a heap element out and vacate the slot the scan reads; this
    corrects the latent Lomuto endgame bug (`pivot T = a[hi]` then
    `a.swap(i, hi)` read the vacated slot for heap T). **Comparator
    parameters removed from the whole machinery** (user ruling, Sep 24): the
    sort is operator-resolved — `partition3By` / `sortRangeBy` / `quickSort`
    write `<` and `==` literally, the compiler resolving `infix_operator<` /
    `infix_operator==` for `T` per instantiation; `sortBy` is dropped in
    favor of wrapper types (`Desc`) providing their own operators; the pair
    must form a **total order** (exactly one of a<b / a==b / a>b). Dispatch
    is `==` first, then `<`, else `>`. **Self-swap is identity** — the
    checker elides `swap(a, i, i)`'s move trio (a C9/R4 carve-out), settling
    the self-swap already latent in Lomuto's first iteration. Note 3 TODO
    closed: all-equal input is one pass (O(n)); note 4's uint-guard
    discipline restated for the two recursion ranges (`r.lt > lo`,
    `r.gt < hi`) and the `i == lo` / `gt` totality invariant. Usage: `Point`
    gains `infix_operator==`, descending floats become `Desc` with reversed
    operators. — OWNERSHIP_DRAFT missing-rules + decision C13.
21. Normative-core consolidation + Go-runtime mapping sync (ruling C14):
    `QUERY.txt` renamed to **`OWNERSHIP_RULES.md`** and rewritten as the
    consolidated normative core — the original questionnaire rules preserved
    verbatim in intent (§O) with the C1–C13 rulings folded in as rules
    (§1–§12), outranking every example; `OWNERSHIP_DRAFT.md` re-billed as
    the deliberation / resolution record (header + premise updated).
    `GO_RUNTIME_MAPPING.md` synced to the whole rule set: header pin
    refreshed, vocabulary + deliberate-difference tables extended, and
    per-rule sections added for C10 (intrinsic errors / `Result[T]` / flat
    `catch` ↔ Go's `(T, error)` / `if err != nil`; `panic`=abort vs unwind),
    C11 (`is` deep walk / view-only binding ↔ `errors.Is` / `errors.As`;
    opaque `causes` vs `errors.Join` — Go *walks* Joined chains, the spec
    refuses; `==`-on-errors CE vs Go's sentinel equality), C12 (reflection
    read-only by shape ↔ `reflect`; `toJson` value-level failures ↔
    `*UnsupportedValueError` for NaN/±Inf; `JsonParseError.offset` ↔
    `*json.SyntaxError.Offset`; JSON-shaped constraint ↔ struct tags; arena
    &-create ↔ heap allocation), and C13 (operator-resolved ordering ↔
    `sort.Slice`'s closure comparator; pdqsort (1.19+); `SliceStable`'s
    aux-buffer O(n log n); Go's copy-swap vs the spec's view-pinned pivot
    and self-swap elision). Housekeeping: OWNERSHIP_DRAFT C7 entry's "`toJson`
    as written" corrected (C12 rewrote it); README checker list gains the
    self-swap-elision carve-out; sketch citations (QUICK_SORT note 1,
    LINKED_LIST notes) repointed to `OWNERSHIP_RULES.md` §6. — decision C14.
22. Stable merge sort (ruling C15): new conformant sketch `MERGE_SORT.md` —
    bottom-up (no recursion), stable, `infix_operator<` **only** — the
    merge's test is "take left unless right < left", so `==` is never
    consulted (the opposite half of quickSort's C13 contract). **New
    mechanism established**: the element move-append `buf += a[i]` on owned
    `[]T` — moves the value into a freshly grown arena slot (`string +` /
    `bytes +=` precedent; growth already the single sanctioned invalidation
    point for views into owned buffers), now stated as a normative rule in
    OWNERSHIP_RULES.md §5. Arena discipline: each bottom-up level wrapped in
    `{ … }` (C7) bounds peak live arena to O(n) with O(n log n) churn freed
    at level boundaries; `buf` never escapes, so its slots may die vacated
    (repair-before-escape governs escaping containers); the right-run tail
    is never moved — when the left run exhausts the write head sits exactly
    on the right head, so any tail write would be self-moves (C13 identity),
    and the code omits them. Usage: `Item { key, seq }` stability demo (seq
    stays ascending among equals — the thing quickSort would permute);
    `Point` sorts with `<` only. README `## Array declaration` gains the
    move-append spelling; QUICK_SORT note 8 repointed (the stable sort is
    now MERGE_SORT.md); GMP gains the append-mapping row. — decision C15.
23. Hash map + binary search (ruling C16): new sketch `HASH_MAP.md` — a
    generic open-addressed map over `Slot { entry Optional[Entry[K, V]] }`:
    an empty bucket is the real value `None`, never a null pointer and never
    a *vacated* slot (a vacated slot is an unreadable tracked non-value, the
    wrong storage for a long-lived table). **New mechanism — in-scope
    `hash`**: `hash func (k const *K) uint`, resolved per instantiation like
    the ordering operators (user ruled this over a Hashable marker), with
    built-ins for the primitive keys. One unprovable contract: **keys equal
    under the in-scope `==` must hash equal** (congruence). `put` moves the
    key/val in; `get` returns `Optional[const *V]` (views invalidated by
    growth — the single sanctioned invalidation point); delete is
    backward-shift with the shift test `dh == 0 or dh > dr` (dist-based,
    uint-safe); grow rebuilds by moves into `nb []Slot[K, V]` and assigns
    `m.buckets = nb` — the old table dies in place. Round rulings:
    **assignment drops the previous occupant in place** (compiler-generated
    dealloc, never a built `dispose`), and **a dropped container may hold
    vacated slots** (repair-before-escape governs escaping containers only)
    — both now normative bullets in OWNERSHIP_RULES.md §6. Query keys
    auto-borrow into `const *K`; `probe` inspects with `match &slot.entry`,
    the shift takes by owned-slot consumption. Companion sketch
    `BINARY_SEARCH.md`: `search` / `lowerBound` on `a const *[]T`, `<`-only
    (equality = "less in neither direction"), half-open `[lo, hi)` uint-safe
    bounds, `Optional[uint]` absence as data, read-only. — decision C16.
24. Binary codec (ruling C17): the reopened `bytes.from(v)` option becomes a
    sketch `BINARY_CODEC.md` — reflection-driven whole-object `toBytes` /
    `fromBytes` under the C12 shape (scalars, string, enum, array, struct;
    no reachable `*T` / `any` / disposable): one shape, two encodings.
    `toBytes func [O] (obj const *O, n := 0) Result[bytes]` — the `toJson`
    const-view walk, output an arena `bytes` buffer; `BinaryWriteError
    { message, cause }` for a non-finite float or the depth cap.
    `fromBytes func [O] (b const bytes) Result[*O]` — success &-creates the
    graph in the caller's statement-block arena (C7), failure
    `BinaryParseError { message, offset, cause }` carrying the parser
    cursor. Wire pinned `Endian.big`; enum arms are length-prefixed *names*
    (not ordinals); struct fields positional, append-only evolution. The
    sketch's one new normative principle, adopted into OWNERSHIP_RULES.md
    §10: **a coded parser bounds-checks before every intrinsic read** — the
    `as*` past-the-end panic (a programmer-bug defense in general) must stay
    unreachable from hostile input; a truncated frame is data,
    `BinaryParseError`. README `## Bytes` de-scope note repointed to the
    sketch. — decision C17.
25. Conformance sweep (C18): README failure construction unified on
    `Error(kind { … })` — the C12-combinator spelling wins over the earlier
    bare-kind returns (NumberError, SocketError ×3, plus the `is`-section
    prose wrapper) — now a normative line in OWNERSHIP_RULES.md §8. Fixes:
    `truncateRead : func` → `truncateRead func`; the Bytes section's "stays
    an option" note rewritten to cite `BINARY_CODEC.md`. Rules clarifiers:
    OWNERSHIP_RULES §6 gains the declaration-defaults /
    assignment-drops-occupant / dropped-container-may-hold-vacated bullets
    (from the hash-map round), §11 gains the in-scope `hash` + congruence
    contract, §10 gains the binary codec line. GMP: vocabulary rows +
    C16–C18 sections; the stale C13 row "a stable sort is 'to be written'"
    repointed to MERGE_SORT.md (C15). Observations, not changed: the legacy
    `s.fields(e1)` / `e.enumerators(e1)` reflection spellings coexist with
    C12's `s.fields` + `f.value(obj)` (the Metaprogramming section stays
    authoritative); sized-array declaration `a [10]int` remains the one
    unrulled area (move-append is the sanctioned growth, C15). —
    decision C18.
26. `T?` / `T!` shapes (ruling C19): README core migrated from
    `Optional[T]` / `Result[T]` to the postfix shapes — the ``## `!` and
    `?` `` section rewritten as ``## `T?` and `T!` ``; every
    `Optional`/`Result`/`Some`/`None`/`Ok`/`Error(…)` spelling in the
    abstract, operators index, pattern matching, control flow (new
    checked-form `if/loop …?` head), try/catch (blocked `try { … }`;
    bare `!` inside a guarded scope now settles at the region's catch),
    error kinds (`Error(kind { … })` → bare `return kind { … }`),
    Generics, toJson/fromJson (`string!` / `*O!`), the socket server
    (newListener/accept/readLine/write/echo/serve/main), Bytes manual
    codecs (`uint!` / `Request!`), channels/select (`(<-ch)?` absence
    arm; `loop s := <-ch?`), the checker list, and the dispose section.
    Normative mirror: OWNERSHIP_RULES §6 (the `T?` default / `{}`),
    §8 (shapes, auto-wrap, obligated-unwrap, `!`-settles-in-region),
    §10 (`bytes!` / `*O!`), §12 (match ban, unwrap obligations).
    GMP: vocabulary rows + a C19 section. Sketches (HASH_MAP,
    BINARY_SEARCH, BINARY_CODEC, LINKED_LIST, LISTEN.md) migrate in a
    follow-up round — the user scoped this round to core spec + rules. —
    decision C19.
27. Sketch migration to the C19 spelling (follow-up to C19): the shipped
    sketches drop every `Optional[T]` / `Result[T]` / `Some` / `None` /
    `Ok(v)` / `Error(kind { … })` spelling for the postfix shapes.
    `HASH_MAP.md`: `Slot { entry Entry[K, V]? }` — an empty bucket is the
    `T?` default absence (`{}`), never a vacated slot; reads (`probe`,
    `get`, the shift's decision) unwrap through a view —
    `if kv := (&slot.entry)?` binds a const view, nothing moves (the
    README-cited container-inspection pattern); mutation consumes through
    owned-slot `?` in the checked head (`takeEntry`, `grow`'s reinsert),
    leaving the field *absent* — a real value — so the vacated-slot
    discipline is gone from the table entirely; `entry = {}` clears, `=`
    re-wraps, `get`/`remove` return `const *V?` / `V?` with bare `return`
    + auto-wrap; notes 2/3/5 rewritten (all-absent drop, no vacated state).
    `BINARY_SEARCH.md`: `search … uint?`, `return i` / bare `return`;
    usage matches become `if i := a.search(3)? then … else`. `BINARY_CODEC
    .md`: `toBytes … bytes!` / `fromBytes … *O!` / `need … uint!`; failures
    are bare `return BinaryWriteError { … }` / `return BinaryParseError
    { … }`, success `return b` / `return obj` (mirrors the migrated
    toJson/fromJson), the string arm ends `return s`. `LINKED_LIST.md`:
    signatures to `T?` / `*T?` / `*Node[T]?`; the Haskell combinator
    section (map/andThen/orElse) removed — each needed `match` on the
    shape, now a compile error — replaced by a C19-forms section, and the
    usage rewritten with the checked `if …?` heads and `??` (both element
    demos; the decay spelling is gone); the `maybe` proposal now desugars
    to a checked-if chain; notes 1/2/6/9/11 re-spelled; the demo's stale
    pop sequence arithmetic corrected (after popValue(10) the list is
    [5, 20], popFront yields 5). `LISTEN.md` (the socket sketch the README
    server descends from) migrated the same way: `uint?` /
    `Listener!` / `Connection!` / `string!` / `uint!`, bare `return kind
    { … }` failures, `return v` auto-wrap successes, the blocked
    `try { loop { ch := socket.recv(conn.fd)! … } }` readLine, and
    `portFromEnv … uint?` with bare `return` absence (its `?? 8080` call
    site predates the round and stays). GMP: C16/C17 sketch rows and the C12
    toJson row re-spelled to the postfix shapes. Same round, post-review:
    LINKED_LIST `remove` → `T!` (sentinel removal is `NotInListError`, read
    through `try … catch _` in `popValue` and the `maybe` desugar — failure
    maps to absence; notes 3/9 re-spelled), LISTEN `write` unwraps
    `socket.send` with `!`. — follow-up to C19.
28. No user abort verb (decision C20 round): the four user-callable panic
    sites are gone. `HASH_MAP.md`: takeEntry → `Entry[K, V]?` with the
    single-statement unwrap-propagation `return m.buckets[pos].entry?`; the
    probe row becomes `Probe!` with a declared `TableFullError` (the
    full-table backstop is now a reported failure, `return TableFullError
    { message = "hash table is full" }`) and get's fallthrough is a bare
    `return` — the map is panic-free. Reading `Probe!` forces guarded
    scopes on all three callers: `get`/`put`/`remove` open with
    `try p := m.probe(key)` and settle the (unreachable under the load
    contract) failure with `catch _`: get/remove map it to absence, void
    `put` skips the insert. Inside those regions the `takeEntry` reads move
    from `?`-propagation to checked heads (`if e := m.takeEntry(p.pos)?
    then …`) — only *bare* `?` is banned inside a guarded scope (first
    in-repo use of a checked head in a region; the reading is pinned for
    the record). `README.md`: the server main's catch-and-`panic("server
    cannot start")` reshapes to `l := newListener(cfg.address, cfg.port)!`
    — main is exempt, failure aborts, "cannot continue" is *not catching*;
    the checker list and OWNERSHIP_RULES §1/§8 gain the compile-error rule;
    LISTEN.md's prose follows. GMP gains the C20 section. — decision C20.
29. Modules, linking, and output fallibility (decision C22 round): README
    gains `## Modules and globals` — `module name` prefix declarations
    (several per file), top-level definitions global with executable
    statements packed into a synthesized `module_initializer` (run from the
    main module's initializer section in module-definition order,
    earlier-included imports first), visibility across a direct import edge
    only, and the `#compiler.private` link directive — plus "Output: `out`
    aborts, `log` reports": the print family `out.println`/`out.print`/
    `out.error` aborts on write failure (a C20 backstop), while the
    non-panicking surface lives in `runtime.log` with the same names
    (`uint!`, read via `!`/`try`). OWNERSHIP_RULES §12 gains the module/
    private/output bullets; the C20 backstop lists in README, §12, and GMP
    gain `out.*`'s write failure; the `runtime("basic")` comment lists
    `runtime.log`; GMP gains the C22 section. — decision C22.

---

# Appendix — decision record (interview + follow-ups, Thu Sep 24)

1. **C1 dispose**: explicit only. The compiler generates deallocation
   machinery *inside* the dispose body; the call is always written by the
   program; checker verifies exactly one dispose/move-out. Q3 reworded
   accordingly. RAII/compiler-called dispose rejected. **Remark (Sep 24):**
   when a variable's type defines *no* `dispose`, the compiler generates the
   deallocation machinery at the end of the variable's scope (R1).
2. **C2 lifetimes**: syntactic containment — no lifetime inference; a mutable
   view may go only to *called* functions. Q9 restated morphologically.
3. **C8/C5 by-value params**: **value params also move** (Rust-style) —
   Copyable values copy in, non-Copyable values must move in; `&T` remains the
   explicit reference-form transfer. Read-only uses must be `const string`.
4. **C3 atomics**: join the refcounted exception (mutex, channel, atomic are
   uniformly refcounted cells; dealloc machinery at the end of the lifetime
   the handle appeared in).
5. **C6 channel close**: the channel can be closed by **any coroutine** —
   all holders co-own a mutable view to the cell, implemented with
   synchronization under the hood. Refcount manages cell *memory*; close is
   runtime-checked; double-close aborts.
6. **Heap lists**: `take()` detach with relocation + dead-node semantics.
7. **C7 function-body arenas (ruling)**: a function body is not an arena.
   `&`-creations and owned buffers inside a callee land in the nearest
   enclosing *statement-block* arena — the caller's — bulk-freed at that
   block's exit; owned locals still get per-variable deallocation at the end
   of the function body (R1). Keeps `newList`/`pushBack` as written (`toJson`
   later rewritten — decision C12); README "Where memory lives" amended;
   LINKED_LIST note 7 restated as a rule.
8. **C9 slot-take (ruling)**: moving a value out of a live container slot is
   defined — the slot becomes *uninitialized* (a tracked non-value); reads
   are compile errors (R4) until a move-in **reinitializes** it (the one R4
   carve-out, mirroring Rust's "prevents further reads until it is
   reinitialized"); uninitialized slots must be repaired before the container
   escapes. `take` on an unlinked node keeps dead-node semantics (unreachable,
   reinit never applies). Closed QUICK_SORT's in-place slot-swap edge: the
   existing `swap` body sorts heap elements unchanged; moves re-home backing
   within the caller's statement-block arena (C7), so nothing allocates.
9. **C10 intrinsic errors (ruling)**: errors become an intrinsic kind with
   payload fields (`X error = { … }`, non-disposable fields only);
   `Result[T]` drops its error parameter — unifying the spec's conflicting
   `Result[string, error]` and single-arg `Result[Connection]` forms and
   dissolving the per-region uniform-E rule; `catch e` binds the intrinsic
   error (any error, by construction); kind tests are `e is IOError` with
   compiler-verified narrowing; wrapping via `cause error`; handlers are
   checkable but not exhaustive (Go-style). Modeled on Go's `error`
   interface and the `errors.Is` (value/sentinel) vs `errors.As` (type-test)
   split — grounded in the Go source. (Missing rules section + applied-fix
   #17; README try/catch and `!`/`?` sections rewritten.)
10. **C11 error-handling rulings (interview, Sep 24)**: (1) `is` is deep —
    kind test walks the `cause` spine top-first, first match wins (Go's
    `errors.Is` walk); (2) Model B — a true `is` binds the found member to a
    new name (`e is IOError io`), `e` never changes referent; (3) bound
    names are view-only — no extraction, `return io` is a compile error,
    chains are append-only (wrapping the top is the only transform);
    (4) built-in `Error { message string, code int }` is public — intrinsics
    fill it from errno, user-constructible, always a possible cause;
    (5) `causes []error` aggregates are opaque to `is` (explicit iteration
    only); (6) `loop i, x` binds the iteration ordinal, 0-based,
    machinery-maintained, uniform; (7) `==` on error values is a compile
    error — sentinels are payload-less kinds tested with `is`. Corrects the
    C10 "narrowing" wording (binding, not narrowing).
11. **C12 reflection IO (follow-up, Sep 24)**: (1) reflection access is
    read-only by shape — every reflected value is a const view (heap shapes)
    or a Copy scalar; enforced via `const *O` parameters, consumption through
    const is CE; (2) `toJson` keeps `Result[string]` — failures are *values*
    (non-finite float, depth cap → `JsonWriteError`), not types; (3) the
    JSON-shaped constraint at instantiation rejects reachable `*T`/`any`/
    disposable fields — serialization cannot cycle by construction, so no
    pointer arm or cycle machinery; (4) parse direction added as `fromJson[O]
    (json const string) Result[*O]` — success &-creates the whole graph in
    the caller's statement-block arena and returns a view (newList/C7);
    failure = `JsonParseError { message, offset, cause }` under C11
    machinery. Answers: keep Result; add fromJson; reject pointers at
    instantiation.
12. **C13 three-way partition (follow-up, Sep 24)**: (1) **view-pinned
    pivot** — the median is compared through `const *T` into its own slot
    `a[hi]` and never copied or moved; a value pivot would move a heap
    element out of its slot (vacated-slot CE) and the scan reads `a[hi]` —
    the ruling also corrects the prior Lomuto endgame bug (`pivot T = a[hi]`
    then `a.swap(i, hi)` read the vacated slot); (2) **`==` joins the
    ordering contract** — three-way dispatch is `==` / `<` / else `>`:
    `quickSort` resolves `infix_operator<` AND `infix_operator==` from
    scope; the machinery is **comparator-free** — `partition3By` /
    `sortRangeBy` / `quickSort` write `<` and `==` literally, the compiler
    resolving them per instantiation (user ruling: no comparator
    parameters); the explicit-ordering entry `sortBy` is dropped — reversed
    or by-field orderings become wrapper types with their own operators
    (`Desc`); together the operators must form a total order, which also
    keeps the `>` branch from firing at `i == lo`
    (uint-safe gt; incomparable values like float NaN cluster on the `>`
    side); (3) **self-swap is identity** — `swap(a, i, i)` is elided by the
    checker (a slot-take C9/R4 carve-out), surfacing the self-swap already
    latent in Lomuto's first iteration and made frequent by the three-way
    (`swap(lt, i)` at i == lt, `swap(i, gt)` at i == gt); (4) **boundaries
    by value** — `partition3By` returns the two-uint Copy struct
    `Range { lt, gt }`; the pinned pivot at `a[hi]` sorts with the right
    subrange. Answers: view-pinned; require `==`; Range struct; elide
    self-swap. Follow-up (same day): **no comparator parameters anywhere** —
    the machinery writes `<` / `==` literally, compiler-resolved per
    instantiation; the explicit-ordering entry `sortBy` removed, orderings
    expressed as wrapper types (single sort, operator-native).
13. **C14 normative-core policy + mapping (Sep 24)**: (1) the normative core
    is a single renamed file — `QUERY.txt` → **`OWNERSHIP_RULES.md`** —
    consolidated: the original questionnaire rules kept (§O, verbatim in
    intent) and the C1–C13 rulings folded in as dense normative rules
    (§1–§12). It outranks all examples; `OWNERSHIP_DRAFT.md` is the
    deliberation / resolution record (title + premise updated). (2)
    `GO_RUNTIME_MAPPING.md` must track the whole rule set: header pin
    refreshed to the core, §1 vocabulary and §4 deliberate-difference rows
    extended, and per-rule mapping sections added for C10–C13. (3)
    Housekeeping: the stale C7 "`toJson` as written" corrected (C12 rewrote
    it), the README checker list gains self-swap elision, sketch slot-take
    citations repointed to `OWNERSHIP_RULES.md` §6. Answers (user): rename →
    OWNERSHIP_RULES.md as the normative core; sync the Go-runtime mapping;
    fix the drift.
14. **C15 stable merge sort (Sep 24)**: (1) **move-append workspace** — the
    user ruling: the merge's scratch is `buf []T = {}` grown by `buf += a[i]`
    element move-appends (the `+=` family: `string +`, `bytes +=`); an
    append moves the value into a freshly grown arena slot, never copies;
    growth is the single sanctioned invalidation point and no view is held
    across it (now a normative rule in OWNERSHIP_RULES.md §5); (2)
    **bottom-up** iteration — no recursion — with every level wrapped in
    `{ … }` arenas, bounding peak live memory to O(n); (3) **stability via
    "take left unless right < left"** — merge sort needs only
    `infix_operator<` in scope; `==` is never consulted (totality of C13
    still required); (4) the **right-run tail never moves** — when the left
    run exhausts, the write head sits exactly on the right run's head, so a
    tail write would be a chain of self-moves elided by the C13 identity
    rule; the code omits them. Answers (user): go for it — move-append
    scratch (C15).

16. **C16 hash map + in-scope hashing (user-ruled, Sep 25)**: the map's
    hasher is `hash func (k const *K) uint`, resolved **per instantiation**
    exactly like the ordering operators — the user chose this over a
    Hashable interface marker and named it `hash`, not `hashOf`; built-ins
    exist for the primitive keys. Congruence: keys equal under the in-scope
    `infix_operator==` must hash equal. The map: open addressing over
    `Slot { entry Optional[Entry[K,V]] }` — an empty bucket is the real
    value `None`, never nil, never a vacated slot; queries via `const *K`
    (auto-borrow); `put` move-in; `get` → `Optional[const *V]` (invalidated
    by growth — the single sanctioned invalidation point); backward-shift
    deletion `dh == 0 or dh > dr`; grow rebuilds by moves, the old table
    dropped in place. Round rulings: assignment drops the previous occupant
    in place; a dropped container may hold vacated slots
    (repair-before-escape governs escaping containers only). Answers (user):
    go — hash in scope (C16).
17. **C17 binary codec (Sep 25)**: the user reopened `bytes.from(v)` as a
    real deliverable after it was de-scoped ("an option, not a
    requirement"). Defined under C12's shape: same O-constraint, same
    value-failure vocabulary, same const-view walk; `BinaryWriteError` /
    `BinaryParseError { message, offset, cause }`; `Endian.big` pinned;
    length-prefixed strings/names/counts; the bounds-check-before-read rule
    keeps malformed input from ever reaching the `as*` panic. Answers
    (user): define it under the C12 shape (C17).
18. **C18 README conformance sweep (Sep 25)**: user-picked sweep thread:
    unify failure construction on `Error(kind { … })`; repair the
    `truncateRead` typo; rewrite the Bytes de-scope note; carry the round's
    rulings into OWNERSHIP_RULES §6/§8/§10/§11; sync GO_RUNTIME_MAPPING.
    Answers (user): sweep the README (C18).
19. **C19 `T?` / `T!` shapes (user-ruled, Sep 25)**: proposal — replace
    `Optional[T]` / `Result[T]` with postfix `T?` / `T!`; `return value`
    auto-wraps by the declared return type (an error-kind value = failure);
    never `match` a Result or Optional; errors handled with try-catch;
    optionals with `if x := foo()? then process(x) else logNoX()` (`then`
    or the first block omittable); bare `return` returns absence. The vet
    surfaced four collisions: maps must still void slots (with `None`
    uncreatable — who writes the empty bucket?), the map's
    inspect-without-consuming needs a spelling, the example's `if cond do`
    conflates `then` (if) and `do` (func/loop bodies), and the if-let
    grammar needed pinning. Answers (user): **core spec + rules first**
    (README + OWNERSHIP_RULES + records + GMP now; sketches follow);
    **keep `then`**; the **`T?` model** — `Optional[T]` under the hood,
    primitives allowed (`x int?`), the checker obligates an explicit unwrap
    before any use (`y := x?`, `y := x ?? 42`, `if x? then process(x) else
    logNoX()`), struct fields `fieldName type?` default to implicit absence
    and must be checked before read. Round rulings: `{}` is the `T?` default
    (absent) — `entry = {}` clears, `x == {}` / `x != {}` test absence;
    view-unwrap (`&x?`) binds a const view of the payload (container
    inspection); `loop x := e?` extends the checked head; a blocked
    `try { … }` is one guarded statement and a bare `!` inside a guarded
    scope fails the region to its own `catch` (it cannot bypass the handler;
    early-return `?` stays banned there — absence has no handler); select
    receive arms spell absence with `(<-ch)?`; auto-wrap on `return` (bare
    `return kind { … }` / `return v`) supersedes the C18 `Error(kind { … })`
    / `Ok(v)` combinators.
20. **C20 no user abort verb (user-ruled, Sep 25)**: proposal — user code
    never calls `panic(...)`; failure is always spelled `T?` / `T!`, and
    only the runtime aborts (main's unwrap failure, `as*`/`peek`/`writeAt`
    past-end, close failure). The survey found exactly four user-callable
    sites; reshaping removed all of them — takeEntry → `Entry[K, V]?` (a
    free slot is absence, `return m.buckets[pos].entry?`), probe → `Probe!`
    with `TableFullError`, get's fallthrough → bare `return`, and the README
    server main → `newListener(...)!` ("cannot continue" is spelled by *not
    catching*). Consequence of `Probe!`: get/put/remove read it through
    `try … catch _` — the map's first guarded scopes — and their `takeEntry`
    reads must use checked heads, since only *bare* `?` is banned in a
    region; void `put`'s handler skips the insert (its surface is pinned by
    the usage statements). The proposed `unreachable` marker was rejected:
    **no user abort verb exists in the language**; only the runtime aborts.
    Answers (user): Option (b) — the pure endgame.
21. **C21 `try` guards a statement or an expression, never a block
    (user-ruled, Sep 25)**: the blocked form (`try { … }`, "a `{ … }` block
    counts as one" per C19) is removed. `try` may be applied only to a
    statement or an expression; the README socket sketch reshapes its
    readLine to `try loop { … }` (a loop is one statement) and echo to two
    flat `try` statements sharing the region's single `catch` — `!` on the
    reads drops because each try unwraps its own `T!`. Aligned in the same
    round: OWNERSHIP_RULES §8 drops the block clause and §12 gains the
    statement/expression-only form (checked); LISTEN.md's readLine mirrors
    the README reshape; GMP's C19 row reworded. **§12 also pinned (C20):
    calling `panic(...)` is a compile error** — panic is intrinsic, failure
    is always `T?`/`T!`, and only the runtime aborts.
22. **C22 modules, linking, and output fallibility (user-ruled, Sep 25)**,
    from the TODO list: (1) `module name` is a **prefix declaration** —
    several modules per file, top-level definitions until the next
    `module`; (2) names and code outside any function are **global**;
    executable top-level statements are packed into a synthesized
    `module_initializer` per module and called from the initializer section
    of the main module — the first thing a binary runs — in **module
    definition order**, earlier-included imports first; (3) visibility is
    the **direct import edge only** — module `B`'s globals are visible in
    `A` exactly when `A` imports `B`, never reversed, never transitive;
    (4) `#compiler.private` on a declaration removes the name from the
    link-visible set (cross-module reference is a compile error); (5)
    output: `out.println`/`out.print`/`out.error` **abort on write
    failure** — a runtime backstop extending C20 — while the non-panicking
    variants live in `runtime.log` with **the same names** (`log.println`,
    `log.print`, `log.error`), typed `uint!` and read through `!`/`try`.
    Answers (user): direct import edge; prefix declaration; `log` module
    with shared names; module-initializer model.