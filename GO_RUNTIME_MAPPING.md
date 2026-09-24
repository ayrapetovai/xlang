# Go runtime internals → spec ownership map

Design notes: how this language's ownership/arena/coroutine model maps onto the
Go runtime (goroutines, scheduler, channels, stacks). Not a 1:1: Go is
GC-based with copy-by-value and ubiquitous aliasing; this spec is
arena-based with single-owner moves. The mapping is *conceptual and
structural* — for each rule, which Go mechanism does the same job, and what
the difference teaches.

Normative spec: `OWNERSHIP_RULES.md` (consolidated core: original Q-rules +
C1–C13 rulings). Deliberations: `OWNERSHIP_DRAFT.md`.
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
| `Result[T]` | `(T, error)` | the pair convention as a builtin shape (now spelled postfix: `T!`) |
| `T?` / `T!` postfix shapes | nil pointer / `v, ok :=` | absence and fallibility as shape suffixes — `Optional`/`Result` under the hood, never `Some`/`None`/`Ok`/`Error` visible (C19) |
| auto-wrap on `return` | explicit `return v, nil` / `return nil, err` | the spec wraps by the declared `T!`/`T?` return type — one construction path (C19) |
| checked `if/loop …?` form | `if v, ok := m[k]; ok { … }` | smart-cast over existence; forward-compatible — Go tests then re-reads (C19) |
| flat `catch` / `try` | `if err != nil` | one guard + one handler vs explicit per-call checks |
| `is` / kind binding | `errors.Is` / `errors.As` | deep chain walk + type extraction, both native (C11) |
| error kinds (declared) | `error` interface + dynamic type | the same dispatch, spelled in the type system |
| JSON-shaped constraint | struct tags (`json:"-"`) | serializability fixed at compile vs by decoder convention |
| element move-append (`buf += a[i]`) | `append(buf, a[i])` | Go copies into the backing; the spec moves the value in (growth = the sanctioned invalidation, MERGE_SORT.md) |
| open-addressed hash map | `map[K]V` | `hmap` + bucket cells (`runtime/map.go`): Go picks the key's hasher from its type — the same "the type knows how to hash itself", spelled as an in-scope `hash` here (HASH_MAP.md) |
| in-scope `hash func (k const *K) uint` | comparable-key internal hasher | resolved per instantiation like the operators (C16); congruence (equal ⇒ same hash) is the contract Go *enforces* on comparable keys and the spec *delegates* to the author |
| binary codec (`toBytes` / `fromBytes`) | `encoding/binary` + `encoding/json` reflection | fixed-width writes ↔ `binary.Write`; shape-derived parse ↔ `json.Decoder`'s arm-by-arm construction; offset-bearing failure ↔ `*json.SyntaxError.Offset` |

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

### C10 — intrinsic errors, `Result[T]`, flat `catch`

| Spec rule | Go counterpart | Note |
|---|---|---|
| Errors are an intrinsic kind with payload fields (non-disposable) | `error` interface; concrete types with fields | Go's payload is just a struct; the spec gates disposables out |
| `Result[T]` (single type arg) | `(T, error)` — the pair convention | From an idiomatic shape to a builtin sum |
| `try <statement>` / one flat `catch e` | `if err != nil { return … }` per call | Spec: one guard region, one handler; Go: unwinding spelled out at every site |
| `!` forces `Result[_]`, `?` forces `Optional[_]`; bare ones are CE inside a guard; `?? default` everywhere; `main` exempt | No Result/Optional; zero values + multiple returns | — |
| Handlers checkable but not exhaustive | nothing checkable; ignored errors via `_ =` | Same pragmatism, statically visible in the spec |
| `panic` = abort, skips defers | panic unwinds and runs defers | Deliberate difference — §4 |

### C11 — `is`, binding, and error kinds

| Spec rule | Go counterpart | Note |
|---|---|---|
| `e is K k` — deep: walks the `cause` spine top-first, first match binds | `errors.Is` — walks `Unwrap()` comparing sentinels | Same "find it in the chain" semantics; Go tests values, the spec tests kinds |
| Binding (Model B): `e is IOError io` binds a *new* name; `e` unchanged | `errors.As` — type-test through the chain, fills a target | As mutates a caller pointer; the spec binds a view-only name |
| Bound names view-only; no extraction; chains append-only | the `errors.As` target is usable and copyable | The spec is stricter — a bound member can't escape the match |
| Built-in public `Error { message string; code int }` | `syscall.Errno` (code + string form) | the spec has one canonical fallback kind |
| `causes []error` **opaque** to `is` (explicit iteration only) | `errors.Join`: its `Unwrap()` returns the slice and `Is` *walks it* | Go deliberately makes Joined chains inspectable; the spec refuses |
| `==` on errors is a compile error; sentinels are payload-less kinds | `err == io.EOF` sentinel comparison | Go equates values; the spec tests kinds — no aliasing trap |

### C12 — reflection and JSON

| Spec rule | Go counterpart | Note |
|---|---|---|
| Reflection access read-only by shape (`const *O`; Copy scalars) | `reflect.Value` inspectable; writes only via `CanSet` pointer games | The spec has no mutation path at all |
| `toJson(obj const *O) string!` / `fromJson(json const string) *O!` — failures are *values* — non-finite float, depth cap → `JsonWriteError` | `json.Marshal`: NaN/±Inf → `*UnsupportedValueError` (wrapped) | Both error *per value*; the spec enumerates kinds in the intrinsic error (postfix shapes, C19) |
| `O` JSON-shaped at instantiation: no reachable `*T`/`any`/disposable — cycles impossible by construction | pointers followed; cycles detected at run time and error | Go guards cycles at runtime; the spec by construction |
| `fromJson` success &-creates the graph in the caller's statement-block arena, returns a view | `json.Unmarshal` allocates on the GC heap | Placement differs; shape is the same |
| Failure = `JsonParseError { message, offset, cause }` | `*json.SyntaxError { Offset int64 }`; `*json.UnmarshalTypeError { Value, Offset }` | Near-identical: kind + offset (C11 machinery both ways) |
| Field metadata (`#json.…`); enum arms `"%q{e.name}"` | struct tags `json:"-"`, `json:"name"`; named strings marshal plainly | tags are the Go original |

### C13 — sorting, operators, slot discipline

| Spec rule | Go counterpart | Note |
|---|---|---|
| Ordering = the element type's `<` and `==` in scope, resolved per instantiation; machinery comparator-free | `sort.Slice` takes a comparator closure per call | The philosophical fork: Go trusts the caller's closure; the spec trusts the type's operators |
| Three-way (Dijkstra) partition, median-of-three | pdqsort (1.19+): insertion + quicksort + heapsort hybrid | both degenerate-proof, via different mechanisms |
| Not stable by default; the stable sort is now MERGE_SORT.md (C15) | `sort.Slice` unstable; `sort.SliceStable` O(n log n) via auxiliary buffer | the same tradeoff Go offers by API choice — the spec spells both as sketches |
| Pivot view-pinned (`const *T` into `a[hi]`); heap elements sort by moves | element values copied freely; swap is copy | Go can copy because it has no uninitialized-slot concept — exactly the gap the spec's pivot avoids |
| `swap(a, i, i)` is identity — checker elides the move trio | `i == j` swap is a harmless no-op copy | the spec's elision is a linear-logic carve-out (§6) |
| Adversarial orderings = wrapper types (`Desc`) with their own operators | wrapper types *or* functional comparators | the spec has one ordering source per type |

### C16 — hash map, hashing, binary search

| Spec rule | Go counterpart | Note |
|---|---|---|
| Open addressing over `Slot { entry Entry[K, V]? }` — an empty bucket is the `T?` default absence (`{}`), never nil, never a vacated slot | `map[K]V` buckets; missing-key reads return the zero value | Go's map has no "empty slot" concept — absence reads as zero; the spec makes absence a first-class value (its default is the shape's own, C19) |
| In-scope `hash func (k const *K) uint`, resolved per instantiation | hasher chosen from the key type (`runtime/type.go`); `maphash` for strings | same "the type hashes itself"; Go *enforces* equal ⇒ same hash, the spec delegates it to the author as a contract (congruence) |
| `put` moves key/val in; `get` → `const *V?`, invalidated by growth | `m[k] = v`, `v, ok := m[k]` (copies) | Go copies values in and out; the spec moves in and views out — the map is a container like any other, not a special shape |
| Backward-shift deletion (`dh == 0 or dh > dr`), no tombstones | `mapdelete` marks slots empty (`emptyOne`) with periodic rehash | Go leaves tombstones; the spec shifts to close the gap — no tombstone state to encode when a slot is only full/absent |
| Grow = rebuild by moves; old table dropped in place (assignment drops the occupant) | `growWork` rehashes into a new bucket array, keeps old buckets during the increment | Go keeps dead buckets alive across the increment; the spec's arena frees the whole old table at once |
| Binary search: reads only, `<`-only, half-open `[lo, hi)`, `uint?` | `sort.Search` with a caller predicate | Go's `sort.Search` needs a closure; the spec's `search`/`lowerBound` are one ordering, resolved like the sorts (BINARY_SEARCH.md) |

### C17 — binary codec

| Spec rule | Go counterpart | Note |
|---|---|---|
| `toBytes` — const-view reflection walk, wire pinned `Endian.big` | `encoding/binary.Write` with `binary.BigEndian`; `encoding/json.Marshal` | Go's `binary.Write` is manual the way the README's manual codecs are; the spec's `toBytes` is the reflection version with a fixed wire |
| `fromBytes` — bounds-check before every intrinsic read; a short frame = `BinaryParseError { offset }`, never a panic | `encoding/binary.Read` propagates `io.EOF` (data, no panic); `json.Decoder` surfaces `*json.SyntaxError.Offset` | the spec's `as*` panic on short reads by design (programmer-bug defense) — the coded parser must convert data to `BinaryParseError` first |
| Success &-creates the `O` graph in the caller's statement-block arena (C7) | heap allocations; decoder scratch | the parsed graph is arena memory with the caller's lifetime, not GC |
| Length-prefixed frames (u32) for strings, names, counts | explicit lengths / binary varints / protobuf | length prefixes make a *derived* parse checkable in both |

### C18 — conformance sweep (construction spelling)

| Spec rule | Go counterpart | Note |
|---|---|---|
| A `T!` failure is constructed by **auto-wrap on `return`** — `return kind { … }` (C19) | `errors.New` / `fmt.Errorf` return `error` values | both languages settle on one construction idiom; C19's auto-wrap superseded C18's `Error(kind { … })` combinator — the bare return itself is now the one spelling |
| Error payloads non-disposable; unbound-`e` payload reads are compile errors | `error` may carry anything; `errors.As` needs a typed target | — |

### C19 — `T?`/`T!` shapes, auto-wrap, the checked form

| Spec rule | Go counterpart | Note |
|---|---|---|
| `T?`/`T!` are postfix shapes (`Optional`/`Result` under the hood), unmatchable | nil-able pointers / `(T, error)` | absence/failure handled by form — `?`, `??`, the checked `if/loop …?`, `!`, `try … catch`; no user-visible `Some`/`None`/`Ok`/`Error`, no null-like value to create |
| `return v` auto-wraps by the declared return type (payload → success, error-kind → failure, bare `return` → absence) | explicit `return v, nil` / `return nil, err` | the wrap is type-directed and checker-verified; Go spells the pair at every return |
| `if x := e? then … else …`, `if x?` (smart-cast), `loop x := e? do` | `if v, ok := m[k]; ok { … }` | the checked form is the only branch on existence; owned payloads move, view-unwrap (`&x?`) binds a const view |
| `{}` is the `T?` default — `x = {}` clears, `x == {}` tests absence | `nil` / `, ok` tests | no fabricated zero for the shape; emptiness is a real default state (spec §6) |
| bare `!` inside a guarded scope fails the region's own `catch` | layered `if err != nil` with explicit defer | a `try { … }` block loops over fallible reads; `?` stays banned there (absence has no handler) |

### C20 — user code never calls `panic` (C20)

| Spec rule | Go counterpart | Note |
|---|---|---|
| User code never calls `panic(...)` — a compile error; failure is always spelled `T?` / `T!` | `panic()` / `log.Fatal` / `os.Exit` are user-callable | Go hands programmers an explicit abort; the spec routes every failure through the shapes — the only aborts are the runtime's own (`main`'s unwrap failure, `as*`/`peek`/`writeAt` past-end, close failure) |
| `main`'s unwrap failure aborts; the runtime reports the intrinsic error | unhandled error with `os.Exit(1)` | main is exempt from forced returns — "cannot continue" is spelled by *not catching* (`newListener(...)!` in a server main) |

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
| Sentinel equality | `err == io.EOF` legal | Compile error — sentinels are payload-less kinds, tested with `is` |
| Self-swap | Copy no-op | Checker elides the move trio (never vacates the slot) |
| Ordering source | Comparator closure per call (`sort.Slice`) | The type's operators in scope, resolved per instantiation |
| Partition values | Copied freely | View-pinned pivot — `const *T` into the slot; heap elements move, never copy |
| JSON failures | `*UnsupportedValueError`, `*json.SyntaxError` | Enumerated kinds through the intrinsic error — `JsonWriteError`, `JsonParseError` (postfix `string!`/`*O!`, C19) |

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
- Stdlib (concept): `errors` (`Is`/`As`/`Join`, the `Unwrap` protocol, 1.13+);
  `encoding/json` (`Marshal` errors on NaN/±Inf via `*UnsupportedValueError`;
  `Unmarshal` syntax errors as `*json.SyntaxError` with an `Offset`; struct
  tags); `sort` (pdqsort adopted for `Sort`/`Slice` in 1.19; `Stable` /
  `SliceStable` O(n log n) via an auxiliary buffer); `reflect` (the type
  descriptors underlying `encoding/json`).