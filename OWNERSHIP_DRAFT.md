# Memory ownership — the rules (normative: `QUERY.txt`)

**Premise.** Memory management rules are normative; the shipped sketches
(README socket server, `LISTEN.md`, `QUICK_SORT.md`, `LINKED_LIST.md`, the
threads/channels sections, `## Bytes`, reflection) are drafts and must conform
to these rules. Where a draft contradicts a rule, the draft is wrong. The
numbered rules below are `QUERY.txt` verbatim in intent, rendered through the
resolutions recorded in the appendix.

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
   of the function body (R1). Keeps `newList`/`pushBack`/`toJson` as written;
   README "Where memory lives" amended; LINKED_LIST note 7 restated as a rule.
8. **C9 slot-take (ruling)**: moving a value out of a live container slot is
   defined — the slot becomes *uninitialized* (a tracked non-value); reads
   are compile errors (R4) until a move-in **reinitializes** it (the one R4
   carve-out, mirroring Rust's "prevents further reads until it is
   reinitialized"); uninitialized slots must be repaired before the container
   escapes. `take` on an unlinked node keeps dead-node semantics (unreachable,
   reinit never applies). Closed QUICK_SORT's in-place slot-swap edge: the
   existing `swap` body sorts heap elements unchanged; moves re-home backing
   within the caller's statement-block arena (C7), so nothing allocates.