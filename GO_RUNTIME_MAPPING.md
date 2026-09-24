# Go runtime internals → spec ownership map

Design notes: how this language's ownership/arena/coroutine model maps onto the
Go runtime (goroutines, scheduler, channels, stacks). Not a 1:1: Go is
GC-based with copy-by-value and ubiquitous aliasing; this spec is
arena-based with single-owner moves. The mapping is *conceptual and
structural* — for each rule, which Go mechanism does the same job, and what
the difference teaches.

Normative spec: `QUERY.txt` + `OWNERSHIP_DRAFT.md` (R1–R6, C7, C9).
Go facts: runtime as of Go 1.20–1.26 (GMP model unchanged; async preemption
since 1.14; contiguous copy-growing stacks since 1.3).

---

## 1. Vocabulary

| Spec | Go | Go structure/mechanism |
|---|---|---|
| coroutine | goroutine | `g` struct, M:N scheduled |
| `spawn f(x)` | `go f(x)` | `runtime.newproc` → run queue |
| `chan[T]` | `chan T` | `hchan` (mutex + ring + `sudog` queues) |
| `Mutex[T]` | `sync.Mutex` | semaphore (`Semacquire`/`Semrelease`), futex-backed |
| `Atomic[T]` | `sync/atomic` | hardware instructions (`LOCK XADD`, CAS, arm64 `LDADD`) |
| `defer` (block-scoped) | `defer` (function-scoped) | `_defer` list; open-coded (1.14+) |
| `dispose` / `defer v.dispose()` | `io.Closer` + `defer c.Close()` | explicit close idiom |
| arena (statement block) | per-goroutine stack; GC heap | escape analysis; `GOEXPERIMENT=arenas` (retired) |
| view `*T` (borrow) | pointer | GC keeps backing alive — no dangling possible |
| checker (static) | `vet`/`staticcheck` + race detector | compile-time `-race` (ThreadSanitizer) |
| frozen pool (`const string`) | string literals, immutable by convention | read-only data section / rodata |
| move / own / consume | *nothing* (copies + aliases) | the fundamental gap Go doesn't model |

---

## 2. Per-rule map

### R1 — lifetime and destruction

| Spec rule | Go counterpart | Note |
|---|---|---|
| Lifetimes: code block / function body / expression | Go has **no lexical lifetime rule**; objects die at GC | Go's only "scope" lifetime is the goroutine stack: locals live until the function returns (or escape to heap) |
| Explicit `dispose`, checker verifies exactly-one discharge | `io.Closer` idiom + `defer`; **no compiler gate** | Go leaves the obligation to convention; missing `Close()` is silent (fd leak). The spec makes it a compile error |
| Compiler-generated dealloc machinery in `dispose` body | `runtime.SetFinalizer` (heuristic, GC-time) | Finalizers are Go's only latent "dispose" — unpredictable, discouraged. The spec's machinery is deterministic |
| Dealloc at end of variable's scope (non-disposable) | GC frees; per-P stacks are reused via `stackcache` | Go frees *whenever*; the spec frees *where* (lexical) |
| Panic = abort, skips defers | **Panic unwinds and runs defers**, then crashes | Deliberate difference — see §4 |

### R2 — ownership transfers

| Spec rule | Go counterpart | Note |
|---|---|---|
| Value params: Copy in / non-Copy **move in** | Go: `T` param copies every value; slices/maps/chans share backing | Go's reference types (*alias*) are precisely the single-owner violation the spec forbids. Rust/this spec chose moves; Go chose shared mutable pointers |
| `&T` param = move-in | `*T` param = borrow-or-handoff, **no ownership meaning** | Ownership in Go is convention (who calls `Close`?) |
| Returns move out | Returns copy (or alias) | — |
| Literal → pool copy into owned `string` param | String literal → `string` is a slice of rodata; `[]byte("x")` copies | Same *materialization* idea: literals get copied when owned storage needs them |

### R3 — dispose must own; non-owner may not destroy

| Spec rule | Go counterpart | Note |
|---|---|---|
| `dispose` without move-in semantics = compile error | No equivalent; `Close()` on a copy is legal and double-closes | Go's classic fd double-close bug; the spec's handle barrier (`Fd struct { value int }`, non-Copy) kills it by shape |
| Non-owner may not destroy (handle barrier) | No compile-time equivalent; `os.File` guards *at runtime* (closed-state flag; ops after `Close` error) | Go's guard is runtime state, the spec's is shape (views can't hold release) |
| Double-close aborts | Go: `close(closedCh)` panics; second `File.Close()` is a runtime-guarded no-op/error | Go guards runtime; the spec guards statically |

### R4 — consumption, no partial consuming

| Spec rule | Go counterpart | Note |
|---|---|---|
| Ownership transmission = point of no return | Go has no consume; after `go f(x)` or `ch <- x` the caller's `x` is **still usable** | Go = aliasing everywhere; the spec = single-owner. This is the deepest philosophical fork |
| No partial consuming (field consume consumes struct) | Struct field reads are copies in Go | — |
| C9 slot-take: reading a non-Copy slot moves it out; slot uninitialized | Go **zero values**: no slot is ever uninitialized | Opposite design: Go initializes everything (cannot express "empty"); the spec tracks emptiness linearly. Swap in Go copies; in the spec it moves |

### R5 — views and lifetimes (syntactic containment)

| Spec rule | Go counterpart | Note |
|---|---|---|
| Mutable view to *called* functions only; spawn takes ownership moves | Go: pointers cross `go` boundaries freely | Go cannot prevent it (GC makes it *safe*, not *sound*: races remain). The spec's refusal is the strict version |
| Views are thread-local by shape (`*T` never crosses) | Go: `unsafe.Pointer` to a stack is a footgun; everything else is GC-safe | Spec: compile error. Go: race detector at runtime |
| No lifetime inference | Go *does* escape analysis (the compiler's job, not the programmer's) | Same decision (malloc vs stack) made automatically in Go, explicitly in the spec |
| Buffer growth invalidates views | `slice = append(slice, …)` may reallocate; **old pointers stay valid (GC keeps backing)** | Go allows the stale reference to live; the spec forbids it. The spec's rule is the deterministic cousin of Go's append footgun |

### R6 — shared cells (mutex / channel / atomic)

| Spec rule | Go counterpart | Note |
|---|---|---|
| `chan[T]`, `Mutex[T]`, `Atomic[T]` = refcounted handles, mutable view with sync under the hood | `chan` = `hchan{ mutex, ring, sendq/recvq }`; `sync.Mutex` = futex/sema; `atomic` = hardware | **This is literally Go's design**: a channel *is* a mutex with a queue protocol. Your "mutable view with synchronization under the hood" = Go's `hchan.lock` and `sudog` park/unpark |
| Any holder may close; double-close aborts | Any goroutine `close()`s; `close` of closed channel panics; send on closed panics | Matches R6 exactly — channels have no owner in Go either |
| Refcount frees cell at last handle's scope exit | Go: `hchan`/`Mutex`/`Atomic` freed by GC (no ownership, no close obligation for Mutex/Atomic) | The spec adds deterministic destruction; Go leaks-until-GC |
| Guards stay statically gated (`g.dispose()` = unlock) | Go: no Unlock-tracking; vet misses most | Spec wins on safety, Go on flexibility |
| Channel sends move the value | `ch <- v` **copies** into the buffer (or hands `sudog`) | Spec: relocation + consume. Go: copy or alias |

### Crossing a coroutine boundary

| Spec rule | Go counterpart | Note |
|---|---|---|
| Owned values move; backing relocates to receiver's arena | Goroutine stacks are **heap blocks**: a G migrates between M's, its stack *address doesn't move* — only the executor | The spec relocates payload backing explicitly; Go keeps the object put and migrates the thread. Same goal: never dangle |
| Frozen (`const`) shares pool buffer | Strings/slices shared across goroutines freely (GC-kept) | — |
| Views never cross | Any `*T` crosses; safety = GC + race detector | Both ban *mutable shared state*: Go at runtime, spec at compile time |

### C7 — function bodies are not arenas

| Spec rule | Go counterpart | Note |
|---|---|---|
| Callee allocs land in caller's statement-block arena | Escape analysis: non-escaping callee allocs land on the caller's stack frame; escaping ones go to heap | The exact same split — made automatically by Go, spelled out by the spec |
| Owned locals still get per-variable dealloc at function end | Locals die with the frame (stack popped) | — |
| Bulk-free at block exit = no GC, no cycles concern | GC handles cycles; `GOEXPERIMENT=arenas` (1.20) was Go's arena attempt — **on hold indefinitely** | The spec's arena is the lexical-arena idea Go explored and shelved |

### defer

| Spec rule | Go counterpart | Note |
|---|---|---|
| `defer` at block exit, LIFO, before arena teardown | `defer` at **function** exit, LIFO | Spec: block-scoped (matches its block arenas). Go: function-scoped |
| Registered by execution | Go: also by execution, but a deferred call's args are *evaluated at defer time* | — |
| Deferred body infallible | Go: no such restriction (panic in defer → panic chaining) | Spec tightens it |
| Open-coded (cheap) fast path | Go 1.14+ open-coded defers in epilogue | Implementation detail the spec could borrow (compiler lowers defers to scope-exit blocks anyway) |

### Linked-list / container rules

| Spec rule | Go counterpart | Note |
|---|---|---|
| `take()` relocates payload out of a node/slot | `*T` + copy, or `mem.Move`; no ownership verb | — |
| Dead node after unlink+take | Go: node stays reachable via GC-held pointers | Spec makes removal observable; Go keeps ghosts |
| Iterators stable because arena nodes never move | Go: no stable-iterator guarantee; slice/array `*T` can dangle logically when backing is reallocated | The spec's arena gives it as a *feature* |

### IO rules

| Spec rule | Go counterpart | Note |
|---|---|---|
| Async registrations retain buffers ⇒ move or pool-promote, never a view | `netpoll` (epoll/kqueue/IOCP): parked goroutines + ready list; buffers are heap, outlive the registration by GC | Same retention reality; Go papers over it with GC, the spec refuses it at compile time |
| Non-blocking callbacks can't capture views | Go: closures capture freely; GC keeps env alive | — |
| Receive arena: receivers allocate into their coroutine's arena | `chanrecv` copies into the receiver's stack/heap | Same: the receiver's memory, every time |

---

## 3. Go mechanisms worth studying for the implementation

Even where the spec says "no", the *machinery* is directly reusable:

1. **Per-P run queue + work stealing** (`runq`, `runqsteal`, `findrunnable`) —
   the template for a coroutine scheduler: lock-free local queue, global as
   fallback, steal-to-load-balance. No central mutex on the fast path.
2. **`gopark`/`goready` + `sudog`** — park G's with a blocking reason; wake
   puts them back on a run queue. This is exactly "await" for
   channel operations in this spec.
3. **`hchan`** — the reference implementation of the spec's `chan[T]` cell:
   mutex + fixed ring for buffered, `sendq`/`recvq` rendezvous for unbuffered,
   `closed` flag with panic-on-close-of-closed.
4. **`systemstack`/`g0`** — M's bootstrap stack: run scheduler and syscall
   code off the G stack. If the spec has views into the current arena, the
   runtime needs an equivalent "not on user stack" discipline.
5. **`stackcache` per P** — free-stack reuse pools per P; the template for
   per-statement-block arena free lists.
6. **Open-coded defers (1.14+)** — defer registered in the epilogue, zero
   allocation fast path; shows how to lower block-scoped defers cheaply.
7. **Async preemption via signal** — how to deschedule a CPU-bound coroutine
   without per-instruction checks; the spec's checker is static, but the
   runtime preemption still needs this (or cooperative yield points).
8. **`sync.Pool` (per-P `poolLocal`)** — per-P caches; the pattern for
   per-arena allocator caches in this spec.

## 4. Deliberate differences (the spec is *stricter* on purpose)

| Concern | Go | This spec |
|---|---|---|
| Memory safety | GC: no use-after-free, no dangling (but leaks until GC) | Arenas + static checker: deterministic, zero hidden cost |
| Data races | Race detector (runtime, pricey, probabilistic) | Compile error (views never cross) |
| Resource double-close | Convention or runtime panic | Shape-level compile error (handle barrier) |
| Defers on panic | Run (unwind) | Skipped (abort) — panic keeps no invariants |
| Empty slots | Zero values (always initialized) | Tracked uninitialized slots (must be reinitialized) |
| Aliasing | Default (slices, maps, pointers) | Compile error (single owner) |
| When memory frees | Whenever GC runs | Exactly at scope end |

Go's model buys: flexibility, simple language, interop. This spec's model
buys: determinism, no GC pauses, freedom-from-bugs at compile time. The Go
runtime structures above show that the underlying *machines* (scheduler,
channel cells, netpoll, per-P pools) are the same shape either way — the
difference is who is trusted with the rules.

---

## 5. Sources / caveats

- Go runtime internals: `runtime/runtime2.go` (`g`, `m`, `p`), `runtime/proc.go`
  (scheduler, `findrunnable`, preemption), `runtime/chan.go` (`hchan`),
  `runtime/stack.go`, `runtime/netpoll_epoll.go`, `runtime/lock_futex.go`,
  `runtime/defer.go`, `runtime/sema.go`, `cmd/compile/internal/escape`.
- Version notes: contiguous stacks Go 1.3; GOMAXPROCS default = NumCPU from
  1.5; async preemption (SIGURG) 1.14; open-coded defers 1.14; `arena`
  experiment 1.20, proposal on hold indefinitely — never stabilized.
- The G-M-P scheduler model is unchanged through Go 1.26 (as of Sep 2026).