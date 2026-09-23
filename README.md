# Abstract

Must be written in C language to embeddable and to interoperate with native code.
Commas ',' are separators as '\n' and ';'. A statement continues on a following line when that line starts with an operator (`.`, `+`, `&&`, `==`, `->`, ...).
Pointer decay.
Pattern matching.
All pointers are non-null.
Strings and arrays with length.
No shadowing, redefinition instead.
Variables are mutable by default.
Declarations: `name type = value`, `name type` for the default value, `name := value` deduces the type; struct/enum/func bodies use `= { ... }` or `do ...`; literal fields and named call arguments use `name = value`.
Each variable, func parameter of field can be const.
Assignment is a statement, not an expression: no chained `a = b = c`, no `++`/`--` — use `i += 1`.
Other statements are expressions.
Strings can be concatenated and multiplicated like in python.
Data types: `void`, `byte`, `char`,  `int`, `float`, `bool`, `string`, `struct`, arrays, `enum`, `interface`.
Meta types: `type`, `func`, `field`, `pointer`, `value`, `any`.
Function's return type counts for signature.
Function's return type participates in overload resolution.
Generic functions deduce type arguments from call arguments.
Only explicit casts allowed.
For unused variables use '_'.
Channels and coroutines, like in Go language.
Any value crosses a thread boundary unless its shape contains a view; `Atomic[T]`/`Mutex[T]` are the only shared mutable state.
Operators can be overloaded.
No exceptions but stacktraces.
Panic is not recoverable, it destroys the whole application (with stack rollback).
The `match` (aka `switch`) is strictly exhaustive.
Dynamic types dispatching for interfaces, like in Go.
String interpolation with formatting.
Any value can be written to `ByteBuffer` which is suitable everywhere.
Meta-type information is stored in the binary. Types are never erased.
Meta information is stored in binaries, including template functions (packed AST).
Memory ownership: const = shared, owned = unique; per-block arenas free memory; moves and views only.
Resources are released explicitly (`dispose func (v &T)`); `Disposable` is derived from shape, and the owner must dispose or move out.

# Syntax Examples

Every declaration follows one formula: `name type` — an optional `= value`
initializes it, and `name := value` declares with a deduced type. `struct`,
`enum`, `func`, and `interface` are kind words: `User struct = { ... }`,
`foo func (x int) int = { ... }`. Literal fields and named call arguments
bind the same way, `name = value`: `acc Account = { owner = generate() }`,
`fold(array = a, ...)`. Assignment `x = 5` is a plain statement — it yields
no value.

## Line continuation

A statement continues onto the next line when the next line begins with an
operator symbol. This is how long method chains and expressions are split:

```c
a []string = {"  alice", "bob  "}
names := a.map(toString)
  .join(", ")          // one statement, split over lines

sum := 1
      + 2              // one statement, sum == 3
```

## Commentaries

```c

// single line comment

/*
multi line comment
*/

/**
  multi line comment
  a int
  /*
    inner multi line comment
  */
  b := 10
**/
```

The closing literal complements by amount of stars.

## Variable declaration

```c
x int // default value, x == 0
x int = 42
x := 42 // type omitted, deduced from the value
x const float = 3.14

s string // default value s == ""
s := "abc"
s string = "Hello, World!"

s string = string.from(1) // s have type string and is "1"
unsignedVar := uint.from(-1) // unsignedVar is 1
```

## Array declaration

```c
a [10]int
a [size]int
a []int
a []int = {1, 2, 3}
a []int = {
  1
  2
  3
}
r []int = 0..=5 // r is {0, 1, 2, 3, 4, 5}
```

## Pointers

```c
s := "Hello"                            // s string = "Hello"
p := &s                                 // p *string = &s; p points to s
assertTrue(p == s)                      // same strings, p is dereferenced
assertTrue(p == &s)                     // same pointers, s is explicitly converted to a pointer
assertTrue(p.length == "Hello".length)  // auto dereference p, p cannot be null, no null pointers in this language
assertTrue(p == "Hello")                // same values, auto dereference p, and infix_operator== in applied
assertTrue(p == &"Hello")               // same pointers, and infix_operator== in applied
assertTrue(p.type.isPointer)
assertTrue("Hello".type.isVariable)
assertTrue("Hello".type.isConst)
assertTrue(s.type.isVariable)
assertTrue(&s.type.isPointer)

// the `==` for strings (values) could look like this:
infix_operator== func (a const string, b const string) #compiler.inline()
do
  if a.length != b.length then
    return false
  else loop i in 0..<a.length do
    if a[i] != b[i] then
      return false
  true
```

## Operators

Swap values:  `<>`
Address-of: `&`
Arithmetic: `+ - * / % <>`
Shifts: `<< >> >>>` and cyclic shifts: `<<~, >>~`
Logic: `&& || ! < > <= >= == !=`
Bitwise: `& | ~ ^`
Strings operators: `+ < > == !=`, duplicate string `*`
Array access operators: `[:] []`
Function call: `()`
Field access: `.`
Literal fields and named arguments: `name = value`
Assignment: `=` (statement only, yields no value); no `++`/`--` — use `i += 1`
Declaration: `name type`, initialization `name type = value`, deduced `name := value`
Channel send/receive: `ch <- v` (moves/copies a value into the cell), `v = <-ch` or `<-ch` (receive, yields `Optional[T]`)
Coroutine operator: `spawn f(args)` — starts `f` on its own coroutine, returns `void`

## Control structures

### If statement

```c
b bool = true

if b then
  oneLineStatement()

if b then
  oneLineStatement()
else
  oneLineStatement()

// each `else` corresponds to the nearest `then`
if b then
  if b then
    oneLineStatement() // only one statement is allowed for `then`
  else
    oneLineStatement()
else
  oneLineStatement()

if b { // multi line statements allowed for code block `{}`
  firstCall()
  secondCall()
} else {
  firstCall()
  secondCall()
}
```

### Switch statement

```c
match x {
  Some(s) => foo(s)
  None => bar()
}
```

### Loop statement

```javascript
loop { // forever
  q.poll().execute()
}

i := 10
loop i < 10 do
  oneLineStatement()

loop {
  foo()
  bar()
} until i < 10

// special handling for numeric types
loop 0..<10 do
  out.println("prints this ten times")

loop 0..=10 do ... // 11 iterations

loop i := 0; i < 10 {
  oneLineStatement(i)
  i += 1
}

loop i := 0; i < 10; i += 1 do
  oneLineStatement()

loop i < 10 {
  oneLineStatement(i)
  i += 1
}

a []int = {1, 2, 3}
loop x in a do
  oneLineStatement(x)

// this `in` plays only in context of `loop`
loop i in 0..<a.length do
  oneLineStatement(a[i])

outer: loop do
  loop {
    x := random(10)
    if x == 5 then
      break outer // same for `continue` and `yield`
  }
// proposal
loop in in arr {
  // we want to know, whether this iteration is last or first
  // or pre-last or post-first
  // TODO Come up with syntax for that!
  last?
  first?
}
```

## `loop` with `in` 

Operator `in` requires functions to be in scope:

```c
begin func [T](c T) Iterator[T]
end func [T](c T) Iterator[T]
next func [T](it Iterator[T]) Iterator[T]
current func [T](it Iterator[T]) &T

Iterator struct [T struct] = {
  data T
  index int
}
// intrinsic array definition
array struct [T] = {
  values []T
  length uint
}
begin func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data = &array
    index = 0
  }

end func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data = &a
    index = a.length
  }
next func [array[E]] (it Iterator[array]) Iterator[E] do
  Iterator {
    data = it.data
    index = it.index + 1
  }
current func [T] (it Iterator[T]) &T do
  it.data[it.index]

// so user can do
ar []int = {1, 2, 3, 4}
loop it := begin(ar); it != end(ar); it = next(ar) do
  out.println(current(it))
// by this
loop e in ar do
  out.println(e)
```

## Data declarations

### Structures

```c
User struct = {
  id int
  password string #access.private()
}

AccountNumber struct = { value const string }

Account struct = {
  owner User
  account const AccountNumber  // set once at construction
  createdAt const Date         // set once at construction
}

acc Account = {
  owner = { // the field's type `User` may be omitted in the literal
    id = generate()
    password = authentication.genPass()
  }
  account = { "111111111" }
  createdAt = date.now()
}
Permanent const struct = {      // only const instances can be created
  x const int = 42              // const with default value, can't be set at construction
}
p Permanent // all fields are const, the default value will have them
```

### Enumeration declaration

```c
MyBeInt enum = { // implicitly has field with type descriptor
  Somting int = 1
  Empty
}
// in standard library
Optional const enum [T any] = {
  Some(T)
  None
}
```

## Pattern matching

The result of the last calculated statement is returned by `match`.

```c
MyEnum enum = { A, B }
e1 := MyEnum.A
match e1 {
  A => out.println("A")
  B => out.println("B")
}

TheEnum enum = { A(int), B(string) }
e2 TheEnum
match e2 {
  A(x) => out.println("e2's value is %d{x}")
  B(s) => out.println("e2's value is %s{s}")
}

SubEnum enum = {
  ONE(enum = {
    INNER_1(int)
    INNER_2(string)
  })
  TWO
}

se1 := SubEnum.ONE(INNER_1(42))
se2 := SubEnum.ONE(INNER_2("Hello"))
se3 := SubEnum.TWO

enums []SubEnum = { se1, se2, se3 }

loop e in enums {
  match e {
    ONE(INNER_1(i)) => out.println("integer i = %d{i}")
    ONE(INNER_2(s)) => out.println("string i = %s{s}")
    TWO => out.println("nothing to print")
  }
}

fieldNames := match e1.type {
  Struct(s) => s.fields(e1).map(toString).join(", ")
  Enum(e)   => e.enumerators(e1).map(toString).join(", ")
  _ => ""
}

x := random(10)
y := 7
match x {                                // each comma separated expression must be true
  1, 2, 3       => out.println("x in [1, 2, 3], and is %d{x}")
  x % 2 == 0    => out.println("x is even") // applied if the first pattern fails
  x < y, x > 2  => out.println("x is less than y and greater than 2") // same
  _             => out.println("nothing paticular")
}
```

## Function declaration

```javascript
foo func (x int) int = {
  x * x // single statement in the root of func block -> return
}
baz func() int = {
  x := 1
  x    // compiler error, more than one statement: return is mandatory
}

bar func (x int) int do
  oneLineStatement(x, 1)

// method
bark func (d *Dog, times int) do loop times do out.println("woof")

foo func (prompt const string) bool = {
  name string
  in.scanf(prompt, &name)
  if name.length == 0 then
    return false
  name.authenticate()
}
```

### Default values for function parameters

```c
foo func (x int, y int = 0) = {}
// foo func (x int, z int) {}   // clashes by signature with first foo, compile error
foo func (x float, z int) = {} // does not clashes by signature with foo for declaration

foo(1)      // ok
foo(1, 2)   // ok
foo(1.0, 1) // does not clash with first foo for calling

foo func(x int, y int) int = { 1 }
a := foo()         // a is int, only one of foo return a value, others return `void`, variable cannot be `void`.
a float = foo() // error, no explicit cast
```

### Function calls

```c
a []int = {1, 2, 3}
fold(a, func (a, b) { a * b }) // implicit return is single expression, types inferred
// call with trailing block, parameter names for lambda from declaration of `fold`
fold(a) {
  a * b
}
fold(array = a, folder = func (a int, b int) int { return a * b }) // explicit
b := foo() // b is initialized by value returned by foo()
b = bar() // b is assigned a value returned by bar()

d Dog
d.bark(10)
```

### Special Functions

```c
// first argument is type, we have nothing to do with it
// intrinsic function, defined in 'basic' package
from func (int, s const string) Result[int] = {
  r := 0
  loop c in s.length>..=0 do
    if '0' <= c && c <= '9' then
      r = r * 10 + int.from(c) - int.from('0')
    else if c == '-' then
       r = -1 * r
    else
      return Error("string value is not an integer number")
  return r
}
// user can define func like this, it allows to do that:

x int
x = int.from("1234")
```

## Memory Ownership

Memory is owned, moved, or borrowed — never shared-mutable.

### Where memory lives

- Every code block is an arena. Runtime allocations (string/[]T buffers,
  `&`-created objects) go to the current arena, which frees them at block exit.
  Cycles are harmless — they are freed en masse, so no GC and no leaks.

```c
{ // code block is a lifetime space
  s1 := struct {}   // anonymous struct value
  s2 := &struct {}  // allocated into the current arena
}
// s1 and the &-created object are freed
```

- String literals and `const` arrays live in a module-global pool, freed only
  on module unload; they can never dangle.
- Panic = abort: no destructors run, memory is abandoned with the app.

### const = shared, owned = unique

- `const` values (including `const string`, `const *T`) are immutable and
  freely shared; copying one shares the buffer at zero cost.
- A non-`const` container is single-owner. "Copying" it is a Move: the source
  binding is consumed.
- Any in-place write (`a[i] = v`, `p.x = v`) requires ownership — writing
  through a `const` view is a compile error.

**Constness widens, never narrows.** A `const` argument binds only to `const`
parameters (`const T`, `const *T`, `const string`): handing a read-only value
to a writable view or a move-in would let the callee mutate or consume what
the caller promised immutable, so it is a compile error. A mutable argument
binds to either kind — passing it to a `const` parameter only widens access.
Returning a `const` view where a mutable one is expected is likewise an
error.

### Copyable types

A type is Copy iff all of its fields are Copy: scalars (int, float, char,
byte, bool), pointers, and structs/enums built from Copyable fields only.
Everything else (string, []T, structs holding them) is a heap type:

- `*T`                 — pass by writable view (modifications visible to the caller)
- `const *T`           — pass by read-only view
- `const T`            — pass by read-only shared value (cheap, no ownership)
- `&T` in a parameter  — move-in: the caller's binding is consumed, callee owns it
- `*T` / `&T` in a return — non-owning view of caller or global memory

`&` position rule: before a type in a parameter = move-in; before an
expression = address-of; in a return type = non-owning view.

```c
point Point = {1, 2}          // Point has only scalars: it is Copy
q := point                     // copy; point is still usable
s := "Hello"
view   *string = &s         // writable view of s
viewRO const *string = &s   // read-only view
foo(s)                        // ERROR: string is a heap type, cannot pass by value
foo(&s)                       // OK — move-in, s is consumed afterwards
```

### The checker (static, move-only)

Compile errors for: use-after-consume, consume-twice, passing a heap value by
value, returning a view of a local, writing through a `const` view,
re-borrowing a consumed binding, and moving a value out of a view binding —
a view has no ownership to give away. No lifetime inference, no alias
analysis.

### Semantics that touch ownership

- `match x` consumes x (bindings move out); `match &x` inspects via views.
- `loop e in arr` binds a view (via `current &T`).
- Closures capture by value (copy const handles, move owned values); they own
  their environment and may escape.
- Channels: sending an owned mutable value moves it; const handles are shared.
  Suspended coroutines keep their arena chain alive.
- clib("m"): C receives a raw `*T` borrow; the caller's arena must outlive the
  call; C must not retain the pointer after return.

### Thread boundary

`spawn` arguments, sends (`ch <- v`), and closures passed to another
coroutine cross a thread boundary. Any value crosses — no type declares
anything — unless its shape contains a view (`*T` or `const *T`): a view is
a borrow of a lexical arena, and the checker refuses to prove that arena
outlives a thread. The check is morphological, at the crossing site;
generics are checked per instantiation.

- **Owned values cross by move.** The runtime relocates their backing
  allocations into the receiving coroutine's arena — nothing dangles.
- **`const` values cross by sharing.** Shared values join the module-global
  pool (freed only on module unload), so they can never dangle.
- **Views never cross.** Iterators, list handles (`*Head[T]`), and any
  struct holding a view are thread-local by shape — they borrow their
  owner's arena, so that is correct rather than a burden.

Mutation requires ownership and sharing requires `const`, so no value can
ever be mutated by two threads at once; `panic` = abort, so a thread cannot
leave shared state in half. Disposable owned values cross by move too — the
dispose obligation rides along, exactly one `dispose()` on the receiving
thread. The only shared mutable state across threads lives behind an
explicit builtin sync tool: `Atomic[T]` (T a scalar: `int`, `uint`, `float`,
`bool`, `char`, `byte` — checked per instantiation), `Mutex[T]`. See
`## Threads and synchronization` below.

## Threads and synchronization

`Atomic[T]` and `Mutex[T]` are the only shared mutable state (see
`### Thread boundary` above). They are intrinsics: const handles to
runtime-managed cells, like a channel handle. The handle crosses a thread
boundary by sharing; the cell lives in runtime memory, never in an arena, so
it can never dangle. The cell stays mutable behind the `const` handle,
exactly as `ch <- v` mutates a channel behind its handle.

### Atomics

Lock-free scalar accounting. `T` must be a lock-free scalar — `int`, `uint`,
`float`, `bool`, `char`, `byte` (checked per instantiation); operations are
sequentially consistent, and relaxed orderings are a later optimization.

```c
atomic.new func [T] (init T) Atomic[T]
load       func [T] (a const Atomic[T]) T                    // a.load()
store      func [T] (a const Atomic[T], v T)                 // a.store(v)
swap       func [T] (a const Atomic[T], v T) T               // a.swap(v) -> previous
cas        func [T] (a const Atomic[T], old T, new T) bool   // a.cas(old, new)
fetchAdd   func [T] (a const Atomic[T], n T) T               // previous; integral only
fetchOr    func [T] (a const Atomic[T], m T) T               // previous; integral only
```

`bool` and `float` get `load`/`store`/`swap`/`cas`; integrals also the
`fetch*` family. `fetchAdd` returns the previous value — that is what makes
it a coordination primitive (`if a.fetchAdd(1) == 0` means "I was first"),
not just a counter.

### Mutex

Arbitrary payload, moved in at creation and owned by the lock. Locking
returns a disposable guard: `dispose` is the unlock, and the resource gate
(`## Resources`) forces exactly one of them — `g.dispose()` or moving `g`
out. A contested `lock()` parks the coroutine like a channel receive, not
the OS thread.

```c
mutex.new func [T] (v T) Mutex[T]
lock      func [T] (m const Mutex[T]) Locked[T]   // parks until acquired
Locked    struct [T] = { value *T }               // view into the cell, valid while locked
dispose   func (g &Locked[T])                     // intrinsic: unlock
```

The guard's view is an alias of the guard binding: after `g.dispose()` the
binding is dead, and returning a view of a local is a compile error, so the
view cannot escape the lock. `panic` = abort, so a coroutine can never die
holding a lock.

### Channels

`chan[T]` is a handle to a runtime-managed cell, like the atomic and mutex
cells. `chan[T].new(n)` creates a channel: `n` slots of buffer;
`chan[T].new(0)` is unbuffered, a rendezvous — send and receive pair up and
transfer the value directly. The handle is an owned box like `Fd` — the
binding that creates it must `dispose()` it, the *close*, exactly once
(`## Resources`). Widening to `const chan[T]` shares the cell: a const
channel handle is a copyable word that crosses thread boundaries by sharing
and carries no close obligation, so only the owner closes. The owner may
also cross by move instead — then the receiving coroutine owns the close.

```c
pong func (ch const chan[string]) = {
  loop {
    match <-ch {
      Some(s) => out.println(s)
      None    => return          // closed and drained — done
    }
  }
}

ch chan[string] = chan[string].new(0)  // new(n): n slots; 0 = unbuffered rendezvous

spawn pong(ch)                    // owned handle widens to const: the cell is shared
ch <- "hello, world"              // a value moves into the cell, then to the receiver
ch.dispose()                      // close: no more sends; pong drains, then sees None
```

The operators are Go's, spelled on ownership. `ch <- v` *moves* `v` into the
cell — the value relocates to the receiving coroutine's arena — while
Copyable values (`int`, `bool`, const handles, `string`, ...) are copied in,
exactly like argument passing. A value whose shape contains a view never
crosses: the `### Thread boundary` rule, so no view can reach another
coroutine through a channel. Sending and receiving take the const handle;
only closing needs the owner.

`v = <-ch` yields `Optional[T]` — the language never fabricates a zero value
where Go's `v, ok := <-ch` would: `Some(x)` is a value, `None` means closed
and drained. `loop x in ch` iterates until `None`. `chan[T].new(16)` is
buffered — sends settle while a slot is free. After `dispose()` a send
aborts, buffered values still drain, and a double-close aborts too.
Unbuffered sends and receives park the coroutine, the same machinery as
`lock()`; when every coroutine is parked with no work left, the program
aborts — Go's "all goroutines are asleep".

### Select

`select` waits for one of several communications to become ready, like Go's
`select`, with one addition: the operation itself is the arm head (`=>`
body, `default` escape), the same shape as `match` arms.

```c
// a worker serving two channels at once — whichever communicates first wins
worker func (jobs const chan[int], results const chan[int], cancel const chan[bool]) = {
  loop {
    select {
      j := <-jobs =>               // fires when a job arrives; j is Optional[int]
        match j {
          Some(j) => results <- j * 2
          None    => return        // jobs closed and drained — we are done
        }
      <-cancel => return           // fires when cancel is closed or delivers
    }
  }
}
```

Ready rules are judged on the `### Channels` cell state:
- receive: ready when a value is buffered, an unbuffered sender is parked, or the
  channel is closed — the arm then fires, its binding `Some(x)`, or `None` once
  closed and drained;
- send: ready when a buffer slot is free or a receiver is parked.

Every case expression (the channel and value operands) is evaluated exactly
once when the `select` is entered, in source order — expressions are
side-effect-free (assignment is a statement), so evaluating them all is
unobservable. When more than one arm is ready, one is picked uniformly at
random, like Go — a busy channel cannot starve the others. If none is ready,
`default` runs; without `default` the coroutine parks (the same machinery as
`lock()`) until one becomes ready.

A send arm selected on a closed channel aborts, exactly like `ch <- v`
outside `select`. A receive arm on a closed channel fires with `None`
forever — its body must exit on `None`, per `### Channels`. `select` covers
channels only: there are no lock or timer arms. An empty `select` is a
compile error — an eternal park has no place under the deadlock rule. A
parked `select` counts like any other park, so "every coroutine parked"
still aborts.

### Example

```c
Counter struct = {
  total uint
  last  uint
}

worker func (id int, counter const Mutex[Counter], ops const Atomic[uint]) = {
  loop {
    ops.fetchAdd(1)                      // lock-free path: no lock
    g := counter.lock()                  // compound update needs the lock
    g.value.total += 1
    g.value.last = id
    g.dispose()                          // unlock — forced by the resource gate
    // do work; a panic here aborts the process, not just this coroutine
  }
}

main func () = {
  counter := mutex.new(Counter { total = 0, last = 0 })
  ops Atomic[uint] = atomic.new(0)       // T deduced from the expected type
  stop := atomic.new(false)

  spawn worker(1, counter, ops)          // const handles cross threads by sharing
  spawn worker(2, counter, ops)
  spawn worker(3, counter, ops)

  loop !stop.load() {
    // keep working; someone flips stop via stop.store(true)
  }
}
```


## Try / catch

`try` guards an operation that returns `Result[T, E]` — and only `Result`:
`Optional[T]` has its own lighter handling and never enters a guarded scope.

The `try-catch` pair from the first `try` to the single `catch` is a lifetime
as code block `{ stements }`.

A guarded scope is the tail of a block:

- the fallible operations stack as `try <statement>` — one statement each, no
  block, all at the top level of the region;
- the region ends with exactly one `catch <name>` — a **label with a
  parameter**. Every statement after it, to the end of the enclosing block, is
  the handler region: flat, no braces, no extra indent;
- `catch` binds only the error value. The handler sees `e` plus whatever
  Copy/const names the block held before the first `try` — nothing declared
  inside the region. `catch` acts more like a label, everything after it is
  a list of statements that considered to be error handler.

```c
readFile func (path string) = {
  status int = 200

  try file := open(path)              // Result[File, Error]
  defer file.dispose()                // registered only because the try above succeeded

  try data := file.readAll()          // Result[[]byte, Error]
  try use(data)                       // success tail

  // both paths settle here — defers registered above fire now
  catch e

  log("read failed: %s{e}")       // the handler is everything after `catch`,
  status = 500                    // to the end of this block — flat
  fallback(status)
}
```

Semantics:

- **Uniform error.** Every try in one region must produce the same `E`.
  Mixing `Result[T, E1]` and `Result[T, E2]` in a single guarded scope does
  not compile — split the regions or unify the errors.
- **The settlement mark.** Both paths meet at the `catch` label. On success
  the flow reaches the mark and the block ends — the handler region is
  skipped. On failure the failed try jumps to the mark with `e` bound,
  registered defers fire, and only then does the handler region run.
- **Names below the mark.** No owned name declared above the mark lives below
  it. A disposable bound by a `try` is already gone by the time the handler
  runs — disposed by its `defer` or moved out. Only `e` and the block's
  earlier Copy/const bindings remain.
- **One region per block.** A block has at most one `catch`; trys do not
  nest, and nothing jumps across block boundaries. An inner block may guard
  its own tail.
- **Panic is not a failure.** `panic` aborts the process; it never jumps to
  `catch` and skips every defer, exactly like it bypasses `dispose`.

## Generics

`[T]` as *parameters* appears only on the declaration side, after the kind
word — `func [T]`, `struct [T]`, constraint forms like `struct [E
Iterable]`, `interface [T[E, _]]`, and specializations like `func
[array[E]]` above. Instantiation puts *arguments* on the referenced name:
`*Head[T]`, `Optional[B]`, `Iterable[T] interface`. Calls never repeat type
arguments: `foo(x)` deduces them from the argument types, and when the
arguments carry no type information (as in `newList()`) from the expected
result type.

Template functions are compiled from scratch for each generic type, and
if a template function needs some function it looks up the scope.

A function compiled with dynamic dispatching does not look for functions:
it requires the type to declare the interface with the marker (below) and
dispatches through the interface's method table.

```c
newList func [T] () *Head[T]
l *Head[int] = newList()   // T = int, deduced from the expected type
l.pushBack(10)            // T = int, deduced from the receiver

x int
x = int.from("1234")       // `int` is an ordinary argument (a type value), not instantiation
```

```c
// E must implement `Iterable` — see the marker below
MyStruct struct [E Iterable] = {
  x E
}

// T is a container, its first generic is the element type
Iterable interface [T[E, _]] = {
  begin   func(c T) Iterator[E]
  end     func(c T) Iterator[E]
  next    func(it Iterator[E]) Iterator[E]
  current func(it Iterator[E]) &E
}

// the marker: MyArray implements Iterable over its element T
MyArray struct[T] = {
  Iterable[T] interface
  items []T
}

// any could be defined like this, but it is intrinsic
any struct [T] = {
  type Type
  value *T
}
```

Inside a struct body, `Name[args] interface` is the conformance marker: the
struct declares it implements the in-scope interface `Name`. The arguments
resolve in the struct's scope, and the interface's container-shaped
parameter is instantiated as *the enclosing struct applied to the marker's
arguments* — inside `MyArray struct[T]`, `Iterable[T] interface` reads as
`Iterable[MyArray[T]]`, so the marker names only what varies. Writing the
container out in full (`Iterable[MyArray[T]] interface`) is valid too. The
marker is checked once, at the struct declaration: every function the
interface needs must be in scope. It is not a member — it takes no layout,
and reflection (for example `s.fields`) never sees it. With the marker,
dynamic dispatch uses the interface's method table; a generic constraint
like `[E Iterable]` is still checked per instantiation.

## Metaprogramming

```c
// Here `json` is a package, name and ignore are functions,
// that take `type` or `field` as arguments. Functions mutate metadata of fields
// Thy are called by the compiler, before compiling dependent functions.
User struct = {
  id       int      #json.ignored()
  name     string   #json.name("username")
  birth    Date     #json.name("dateOfBirth")
  password string   #json.masked(json.mask.first(10))
}

// this json serializer does not pay attention to field metadata
toJson func [O] (obj *O, n := 0) Result[string] = {
  indents := "    " * n
  json := indents + match O {
    String(s) => "%q{s}\n"
    Integer(i) => "%n{i}\n"
    Float(f) => "%f{f}\n"
    Boolean(b) => "%b{b}\n"
    Enum(e) => "%q{e.name + "_" + e.type}: {\n" + toJson(e.value(), n + 1) + "\n}\n"
    Array(a) =>
      subjson string
      loop e in a {
        mayBeComma := if !last then "," else ""
        subjson += indents + "%s{toJson(e, n + 1)}%s{mayBeComma}\n"
      }
      "[\n" + subjson + indents + "]\n"
    Struct(s) =>
      subjson string
      loop f in s.fields {               // s.fields yields const *field — read-only views
        mayBeComma := if !last then "," else ""
        subjson += indents + "%q{f.name()}: %s{toJson(f.value(obj), n + 1)}%s{mayBeComma}\n"
      }
      "{\n" + subjson + indents + "}\n"
  }
  return Ok(json)
}
```

```c
a int = 1
b int = 2
assertTrue(a != b)            // intrinsic infix_operator!=: func(a int, b int) bool
assertTrue(a.type == b.type)  // intrinsic infix_operator==: func(a type, b type) bool
```

### Type descriptors

```c
type enum = {
  Struct(structDesc)
  Enum()
  Func()
  Int()
  Bool()
  Float()
  Array()
}

// `struct`, `enum`, `func`, `interface` are reserved, so the descriptor is
// named `structDesc`
structDesc struct = {
  name string
  package string
  generics []generic
  macros []macro
  fields []field
  layout layout
}

field struct = {
  name string
  type type
}
```

## Connecting libraries

```javascript
#import {
  runtime("basic") // implicitly imported, gives: runtime.out, runtime.process, memory allocator, regexp parser... compile-time.
  lib("fmt", "sync") // regular library, standard or custom, compile-time
  git("git:github.io/username/reponame.git") // pull from the git repository
  source("./libs/source-file.lang") // the language source file, compile-time
  clib("m") // library compiled from C language, compile-time
  virt("./") // virtual source when ran as embedded, compile-time
}
```

## Socket server

A TCP echo server, exercising `Result` for syscall failures, exhaustive
`match`, `&` move-in for single-owner sockets, method sugar, `dispose` for
OS resources (`## Resources` below), `spawn` — the coroutine operator
(used like Go's `go`), and channels (`### Channels`). Only `socket.*`
intrinsics (from `clib("c")`) are sketched beyond the core language.

```c
// A TCP echo server: accept forever, echo each received line back, close.
// OS failures are data — Result, not exceptions. Panic stays for bugs.

// -- the handle barrier: an fd is its own disposable type, not a Copy int
Fd struct = {
  value int
}

// release is the handle's dispose — the only consumer an Fd can have
dispose func (f &Fd) = {
  socket.close(f.value)
}

ServerConfig struct = {
  address string
  port    uint
}

Listener struct = {
  fd Fd           // disposable handle; Listener.dispose is synthesized
}

Connection struct = {
  fd   Fd             // disposable handle; Connection.dispose is synthesized
  peer const string   // read-only shared view of the remote address
}

// -- bind + listen
newListener func (address string, port uint) Result[Listener] = {
  fdResult := socket.listen(address, port)   // intrinsic from clib("c"), returns Result[Fd]
  return match fdResult {
    Error(e) => Error("cannot listen on %s{address}:%d{port}: %s{e}")
    Ok(fd)   => Ok(Listener { fd = fd })
  }
}

// -- accept one connection; failures here are transient, the caller keeps serving
accept func (listener *Listener) Result[Connection] = {
  return match socket.accept(listener.fd) {  // fd through a view: *Fd
    Error(e) => Error("accept: %s{e}")
    Ok(fd)   => Ok(Connection { fd = fd, peer = socket.peerName(fd) })
  }
}

// -- slurp one line (until \n, or EOF with data)
readLine func (conn *Connection) Result[string] = {
  buf string
  loop {
    match socket.recv(conn.fd) {          // view: conn is *Connection
      Ok(ch) =>
        if ch == '\n' then
          return Ok(buf)
        buf += string.from(ch)
      Error(e) =>
        if buf.length > 0 then
          return Ok(buf)               // EOF with data: deliver what we have
        return Error("connection closed: %s{e}")
    }
  }
}

write func (conn *Connection, data const string) Result[uint] = {
  socket.send(conn.fd, data)           // returns Result[uint]
}

// -- `Connection.dispose` / `Listener.dispose` are synthesized from the fd
// -- field; `dispose func (f &Fd)` above is the only leaf body. Call sites:
// -- `conn.dispose()` in echo, `l.dispose()` in main.

// -- one coroutine per connection; `&` moves ownership in
echo func (conn &Connection) = {
  match conn.readLine() {
    Ok(text) =>
      match conn.write(text) {
        Ok(n)    => out.println("echoed %d{n} bytes to %s{conn.peer}")
        Error(e) => out.println("write to %s{conn.peer}: %s{e}")
      }
    Error(e) => out.println("read from %s{conn.peer}: %s{e}")
  }
  conn.dispose()   // the coroutine owns the connection
}

serve func (listener *Listener) = {
  loop {
    match listener.accept() {
      Ok(conn) => spawn echo(&conn)    // the fd's ownership moves into the coroutine
      Error(e) => out.println("%s{e}")
    }
  }
}

main func () = {
  cfg := ServerConfig { address = "0.0.0.0", port = 8080 }
  match newListener(cfg.address, cfg.port) {
    Ok(l) =>
      serve(&l)                // serve borrows a view; we still own the listener
      l.dispose()
    Error(e) =>
      out.println("fatal: %s{e}")
      panic("server cannot start")
  }
}
```

Note: **`spawn` — the coroutine operator.** `spawn f(args)` starts `f` on its
own coroutine and returns immediately, like Go's `go`. Channels and their
operators (`chan[T].new(n)`, `ch <- v`, `v = <-ch`) are the `### Channels`
section above. `&conn` *moves* the accepted connection into the spawned
coroutine — a socket has exactly one owner — and suspended coroutines keep
their arena chain alive, so a blocking `readLine` costs nothing to wait on:
the line buffer lives in the coroutine's arena and is freed when the
coroutine ends. Ownership of the fds works the same way: `spawn echo(&conn)`
moves the connection into the coroutine, so `echo` — not `serve` — owes
`conn.dispose()`; and `serve(&l)` only lends `main`'s listener a view, so
`main` owes `l.dispose()` after `serve` returns. See `## Resources (dispose)`
below.

## Resources (dispose)

Arenas free memory; *resources* — an fd, a file handle, an OS lock — do not.
A type that owns one is **disposable**, and `Disposable(T)` is *derived, not
guessed*:

- **Leaf.** A declaration `dispose func (v &T) = { ... }` marks `T`
  disposable. You write the body once; it is void — nothing receives its
  result, so a close failure is a `panic` (abort). The `&` matters: dispose
  *consumes* its argument, so the checker can prove the resource goes
  exactly once. Declaring dispose also makes `T` non-Copy — a resource must
  never be duplicated, so `const T` (shared value) is forbidden for it too;
  only views `*T` / `const *T` may observe a disposable value.
- **Struct.** `S struct = { ... }` is disposable iff any field's type is
  disposable — this is how the compiler knows a struct holds a resource: the
  property travels up from the fields, with no body inspection. A composite
  `dispose` body is *synthesized* — field disposes in declaration order — so
  you only ever write leaf bodies.
- **Never:** scalars, `string`, `enum`, and views `*T` / `const *T`. A view
  borrows; it never owns, so it never disposes. `[]T` is disposable iff `T`
  is — the buffer itself is arena memory, but the elements carry
  obligations, so a loop-dispose over them is synthesized.

**Handle barrier.** A resource handle is a disposable type of its own, never
a Copy scalar: `Fd struct = { value int }`, `dispose func (f &Fd)`. If the fd
were a Copy `int`, any `*Connection` view could copy it out and close the
copy — the real connection keeps living and the owner's `dispose`
double-closes. Non-Copy closes that: a handle leaves a binding only by
*move*, moves require ownership, and a view binding has none — reading a
non-Copy field through a view yields a *view* of it, which cannot be handed
to the dispose-shaped release. So `close func (conn *Connection) = {
socket.close(conn.fd) }` is undeclarable by shape alone, and release is
unreachable from any view — no body inspection involved. Release operations
(`close`, `kill`, `destroy`) are `dispose`es of the handle type.

At the end of every owned binding's lifetime the obligation must be
discharged by exactly one of:

- `v.dispose()` was called — afterwards `v` is dead, like any consume; using
  it again is a compile error, so double-dispose is impossible; or
- ownership moved out — `&v` passed to a function, `v` returned, `v` stored
  into another binding. The check follows the move to the new owner; or
- `defer v.dispose()` scheduled it — see `## defer`. Scheduled is not called:
  the body runs once, later, at the block's normal exit, and still counts as
  the single discharge. Inside a guarded scope (`## Try / catch`) `defer` is
  the *only* acceptable form, since an explicit call cannot cover the failure
  path.

The check keys on the **last owned use**, not on scope text: a value that
moved out owes nothing, a value that only lent a view still owes. Generic
functions are templates compiled per concrete type, so the obligation is
checked per instantiation — a generic body that neither disposes nor re-moves
its `&T` parameter fails to instantiate for a disposable `T`. Panic bypasses
dispose: abort abandons resources with the app, exactly like memory today.

**Interfaces and disposal.** Every `interface` implicitly carries a `dispose`
member, dispatched through the same method table as any interface call — no
"dispose-capable" category, no "is it disposable?" check, nothing to remember
at the interface declaration:

- An owned binding of interface type faces the same end-of-lifetime gate:
  `v.dispose()` or move-out. The box may hold a disposable concrete, so the
  owner must discharge it — uniformly, for every interface.
- The default dispose body does nothing: a struct with no dispose (a
  non-disposable concrete) still satisfies the member, and disposing its box
  is a no-op that still consumes the binding.
- A disposable concrete overrides the slot with its own dispose (written or
  synthesized), so boxing it by value is always safe — the owner can only
  reach that dispose through the interface.
- Interface values are non-Copy boxes: no `const I` sharing, copying a
  binding is a Move, and `[]I` gets the synthesized loop-dispose.

The *forcing* is the derived struct's: the interface guarantees the hook, the
checker forces every owner to call it or move the value out, and the concrete
struct's dispose body decides what actually happens.

## defer

Deferring is *scheduling*, not disposal: `defer <statement>` or
`defer { … }` schedules its body to run when the enclosing block exits
normally — the end of the block, a `return` / `break` / `continue`, the
settlement mark of a guarded scope, or a failure jump to `catch`. Bodies run
in LIFO order — the last registered runs first — and always before the arena
tears down the block's memory. `defer` is general cleanup, not dispose-only:
any statement may be deferred. A deferred body returns nothing and must be
infallible — it cannot `try`, and there is nothing left to deliver a failure
to.

A defer is **registered by execution**: control passes the `defer` statement
and the body is scheduled. This is what makes try-catch presence invisible —
a defer written after a `try` exists if and only if that try succeeded,
because a failed try jumps straight to `catch` and the defer statement is
never touched. No failure-slice machinery, no path-sensitive tracking of
"which disposables exist on this path".

A disposable bound inside a guarded scope must be discharged with `defer` (or
moved out); an explicit `v.dispose()` covers only the success path, since the
next try can still fail and jump to `catch` before it runs. So the checker
demands a following `defer` that consumes it — a compile error otherwise.
Dispose by defer is still exactly-once: deferring a value that is later
explicitly disposed is an error, and a value moved out carries no pending
defer.

Panic is the only exit that skips defers: `panic` aborts the process, so
deferred bodies never run and resources are abandoned with the app — exactly
like memory today.
