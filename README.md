# Abstract

Compiled to a native binary, with a C ABI for native interop.
The bootstrap is a C program that parses and interprets the language (no coroutines, no compile-time code execution); it runs the self-hosted target compiler — written in the language itself — which compiles the language's source, including its own compiler, to native binaries.
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
Data types: `void`, `byte`, `char`,  `int`, `float`, `bool`, `string`, `struct`, arrays, `enum`, `interface`. Absence and fallibility are postfix shapes — `T?` (maybe-value) and `T!` (fallible) — see ``## `T?` and `T!` ``.
Meta types: `type`, `func`, `field`, `pointer`, `value`, `any`.
Function's return type counts for signature.
Function's return type participates in overload resolution.
Generic functions deduce type arguments from call arguments.
Only explicit casts allowed — casting is `X.from(y)` (there is no cast operator).
For unused variables use '_'.
Channels and coroutines, like in Go language.
Any value crosses a thread boundary unless its shape contains a view; `Atomic[T]`/`Mutex[T]`/`chan[T]` are the only shared mutable state.
Operators can be overloaded.
No exceptions but stacktraces.
Panic is not recoverable, it destroys the whole application (with stack rollback).
The `match` (aka `switch`) is strictly exhaustive.
Dynamic types dispatching for interfaces, like in Go.
String interpolation with formatting.
Any value can be written to `ByteBuffer` which is suitable everywhere.
Meta-type information is stored in the binary. Types are never erased.
Meta information is stored in binaries, including template functions (packed bytecode, with is stripped before linkage).
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

s string = string.from(1)! // s has type string and is "1" — `from` is fallible
unsignedVar := uint.from(-1)! // unsignedVar is 1
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

An owned buffer grows by **element move-append** — the `+=` family alongside
`string +` and `bytes +=` (`MERGE_SORT.md` stages its merge workspace this
way). The value moves into a freshly grown arena slot; copies nothing;
growth invalidates outstanding views, so hold none across it:

```c
buf []T = {}
buf += a[i]   // move-append: a[i] is vacated (slot-take); buf grows in the arena
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
// (intrinsic sketch, `#compiler.inline()`. User-defined operators take
// `const *T` operands — auto-borrowed, non-owning — so the signature below
// is the shape you would spell; see "Copyable types")
infix_operator== func (a const *string, b const *string) #compiler.inline()
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
Channel send/receive: `ch <- v` (moves/copies a value into the cell), `v = <-ch` or `<-ch` (receive, yields `T?`)
Coroutine operator: `spawn f(args)` — starts `f` on its own coroutine, returns `void`
Unwrap (propagate): postfix `!` on a `T!` (returns the intrinsic error from the function, or panics in `main`), postfix `?` on a `T?` (returns absence, or panics in `main`), and fallback `?? default` (keeps going with `default`) — see ``## `T?` and `T!` ``
User-defined overloads: `infix_operator<`, `infix_operator==`, … take `const *T` operands (auto-borrowed, non-owning) — see "Copyable types".

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

### If over a `T?` — the checked form

Reading a `T?` obligates an explicit unwrap; the condition head is the
checked form: the payload binds for the `then`-branch, absence runs the
`else`. The head's `?` never propagates — it branches (``## `T?` and `T!` ``).

```c
if x := m.get(key)? then            // binds payload x for the branch
  process(x)
// the `then`-branch may be omitted — absence runs the else:
if y := m.get(other)? else          // else handles the absent case
  logNoX()
// a named `T?` tested in place rebinds its payload (smart-cast):
if m.get(key)? then
  out.println("hit")
else
  out.println("miss")
// loop while present — the same branching rule:
loop b := q.poll()? do
  process(b)
```

The binding's scope is the branch's block; `else` is optional on either
side (`if x := f()? then stmt` — absence does nothing). Unwrapping an
*owned* payload moves it out (consume); unwrapping through a view binds a
const view of the payload (`if kv := (&slot.entry)?` — the container
inspection pattern, `HASH_MAP.md`).

### Switch statement

```c
handlerResult enum = { Accepted, Rejected(reason string) }

r := handlerResult.Rejected("busy")
match r {
  Accepted => out.println("ok")
  Rejected(reason) => out.println("no: %s{reason}")
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

arr []int = {1, 2, 3}
loop i, x in arr do
  out.println("%d{i}th element is %d{x}")
```

## `loop` with `in` 

Operator `in` requires functions to be in scope; containers are iterated by
**view** — `begin`/`end` take `*T`, and `loop e in ar` auto-addresses the
container (`begin(&ar)`), exactly as method sugar auto-addresses a receiver:

A second binding — `loop i, x in ar` — adds the **iteration ordinal**: `i`
is a 0-based count the loop machinery maintains itself, independent of the
container and uniform for every iterable (for arrays it coincides with the
slot index). Each iteration that starts increments it — `continue` does not
reset it — and each step receives a fresh copy: mutating `i` in the body is
legal, but has no effect on the loop.

```c
begin func [T](c *T) Iterator[T]
end func [T](c *T) Iterator[T]
next func [T](it Iterator[T]) Iterator[T]
current func [T](it Iterator[T]) &T

Iterator struct [T struct] = {
  data *T
  index int
}
// intrinsic array definition
array struct [T] = {
  values []T
  length uint
}
begin func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data = a
    index = 0
  }

end func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data = a
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
loop it := begin(&ar); it != end(&ar); it = next(it) do
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
// there is no user `Optional[T]` — absence is the postfix shape `T?`
// (Option under the hood: the checker's shape is `{ Some(T), None }`,
// but users never spell either arm, and `match` on a `T?` is a compile
// error). Absence is written as `return` (bare), `{}` (the default of a
// `T?`), or the `else` of an `if ...?` form — see "## `T?` and `T!`".
```

## Pattern matching

The result of the last calculated statement is returned by `match`.

`match` covers enums, structs, and type reflection — never `T?` or `T!`:
the absence/failure shapes are handled **by form, not by arms** (the checked
`if ...?` form, `?` / `??`, and `## Try / catch`). `match` on a `T?` or `T!`
value is a compile error (``## `T?` and `T!` ``).

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
// trailing block: one parameter binds as `it`; several declare names before `:`
fold(a) { a, b : a * b }
fold(array = a, folder = func (a int, b int) int { return a * b }) // explicit
b := foo() // b is initialized by value returned by foo()
b = bar() // b is assigned a value returned by bar()

d Dog
d.bark(10)
```

A trailing block is a lambda with *local* parameter names — nothing is inherited
from the callee's declaration. A single parameter binds as the reserved `it`:
`{ it * 2 }`; several parameters declare their own names before `:`:
`{ a, b : a * b }`. Function types in parameter lists carry no names
(`less func (const T, const T) bool`) — a written-out function *expression*
declares them inline (`func (a int, b int) int { … }`).

### Special Functions

```c
NumberError error = { message string }

// `from` is the cast family: there is no cast operator. To cast a Y to an
// X, call `X.from(y)`. The first argument is the target type (a type
// value — see Metaprogramming); the source is taken as an immutable
// read-only view `const *Y` — never consumed, never modified, no copy.
// The result is always fallible `X!`. Predefined for the scalar types
// (`byte char int uint float bool`) and the `string`/`bytes` textual
// conversions; `from` is not reserved — users define their own `from`s.
// Intrinsic `from`s are defined in the 'basic' package.
from func (int, s const *string) int! = {
  r := 0
  loop c in s.length>..=0 do
    if '0' <= c && c <= '9' then
      r = r * 10 + int.from(c)! - int.from('0')!
    else if c == '-' then
       r = -1 * r
    else
      return NumberError { message = "string value is not an integer number" }
  return r
}
// user can define func like this, it allows to do that:

parse2x func (s const string) int! = {
  x := int.from(s)!          // unwrap — failure propagates the intrinsic error
  return x * 2               // auto-wrap: success
}
```

## Memory Ownership

Memory is owned, moved, or borrowed — never shared-mutable.

### Where memory lives

- Every `{ … }` statement block is an arena. Runtime allocations (string/[]T
  buffers, `&`-created objects) go to the arena of the nearest enclosing
  statement block, which frees them at that block's exit. Cycles are harmless —
  they are freed en masse, so no GC and no leaks.
- **A function body is not an arena.** A call opens no arena of its own: the
  callee's `&`-creations and owned buffers land in the nearest enclosing
  *statement-block* arena — the caller's. So a function may return an
  `&`-created object (`newList()` → `*Head[T]`) or wrap one in an owned return
  (`toJson` → `string!`), and it survives the call, dying with the caller's
  block. Owned *locals* still die at the end of the function body — each gets
  its per-variable deallocation there (R1) — even though the underlying buffer
  lives in the outer arena.
- Lifetimes are exactly three — **code block scope**, **function body**,
  **expression (temporary value)**. A temporary not bound to a name dies at
  the end of the enclosing expression; a bound one (`x := f()`) extends to
  the binding's lifetime.

```c
{ // code block is a lifetime space
  s1 := struct {}   // anonymous struct value
  s2 := &struct {}  // allocated into the current arena
}
// s1 and the &-created object are freed
```

- String literals and `const` arrays live in a module-global pool, freed only
  on module unload; they can never dangle.
- A string literal handed to an *owned* `string` parameter materializes a
  copy of the pool bytes into the current arena, then moves — a frozen value
  has no unique owner to move. A `const string` (shared) parameter keeps the
  pool share directly, no copy, no move.
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

### Slots: moving values out

Moving an owned element out of a live container slot is *defined*. Reading a
non-Copy element into a local (`t := a[i]`) or the explicit `take(a[i])`
**moves the element out** and leaves the slot **uninitialized** — a tracked
non-value, not a null.

- Reads of an uninitialized slot are compile errors until it is
  reinitialized (Rust: "prevents further reads until it is reinitialized").
- Assignment `a[i] = v` into an uninitialized slot is a **move-in
  reinitialization** — the sanctioned repair, and the one carve-out from the
  no-return rule of ownership transmission (`### The checker`).
- A slot still uninitialized when its container moves, returns, or leaves
  the function is a compile error: no *observable* empty slot ever exists.
- The checker tracks slot state linearly (initialized → taken →
  reinitialized) — morphological, no inference, like the rest of the checker.

Consequence: the three-line swap (`t := a[i]; a[i] = a[j]; a[j] = t`) moves
heap elements in place — take, then two reinitializations — with no copies,
no `Option` wrapper, and (within the caller's statement-block arena, see
`### Where memory lives`) no new allocations. `take` on an *unlinked* list
node is the dead form: the node's location is unreachable, so
reinitialization never applies there (`LINKED_LIST.md`, note 11).

### Copyable types

A type is Copy iff all of its fields are Copy: scalars (int, float, char,
byte, bool), pointers, and structs/enums built from Copyable fields only.
Everything else (string, []T, structs holding them) is a heap type:

- `T` (value)          — pass by value: Copyable types copy in; non-Copyable
                         types *move* in (the caller's binding is consumed) —
                         Rust-style, no copy ever happens (see below)
- `*T`                 — pass by writable view (modifications visible to the caller)
- `const *T`           — pass by read-only view
- `const T`            — pass by read-only shared value (cheap, no ownership;
                         Copyable types only — heap types share via
                         `const string` / `const []T` or views)
- `&T` in a parameter  — move-in: the caller's binding is consumed, callee owns it
- `*T` / `&T` in a return — non-owning view of caller or global memory

`&` position rule: before a type in a parameter = move-in; before an
expression = address-of; in a return type = non-owning view; in argument
position, `&expr` = address-of (a view) — *except* against a `&T`
parameter, where it is the move-in itself: `spawn echo(&conn)` moves a
`Connection` into the coroutine, while `serve(&l)` only lends `Listener` a
view.

```c
point Point = {1, 2}          // Point has only scalars: it is Copy
q := point                     // copy; point is still usable
s := "Hello"
sc := s                        // ERROR: copying a heap value — string is not
                               // Copy; only moves and const sharing exist
view   *string = &s         // writable view of s
viewRO const *string = &s   // read-only view of s
foo(s)                        // foo func (x string): s is heap, so this is a
                              // move — s is consumed afterwards (foo func
                              // (x &string) is the explicit reference form of
                              // the same move-in)
```

**Values auto-borrow into `const *T`.** A value argument binds to a `const *T`
parameter by implicit read-only view — no copy, no ownership. That is how
`node.value == v` reaches `infix_operator== func (a const *Point, b const
*Point) bool` from two `Point` values, and how comparators are invoked as
`a[mid].less(a[lo])`. User-defined operators (`==`, `<`, …) and comparators
take `const *T`: never `&T` (that moves in) and never `const T` (Copyable-only;
a compile error for heap types). A *mutable* view still requires an explicit
`&`.

A mutable view has a *syntactic* lifetime: it may be passed only to *called*
functions — bodies textually enclosed in the caller's scope. Passing a
mutable view to a spawned coroutine, returning one out of a local, or
storing it beyond the owner's scope is a compile error; a coroutine boundary
accepts ownership, Copy values, frozen values, and the refcounted sync
handles only (see `### Thread boundary`). No lifetime inference.

### The checker (static, move-only)

Compile errors for: use-after-consume, consume-twice, copying a heap value by
value, partial consumption (consuming a struct field consumes the whole
struct), returning a view of a local, writing through a `const` view,
re-borrowing a consumed binding, moving a value out of a view binding — a
view has no ownership to give away — reading an uninitialized slot, moving
or returning a container that still holds an uninitialized slot, reading an
error payload without a kind-bound name, comparing error values with `==`,
declaring an error type with a disposable field, reading a `T?` / `T!`
without an explicit unwrap (`?` / `??` / the checked if-form for `T?`;
`!` / `try` for `T!`), `match` on a `T?` / `T!` value, the stacked
shapes `T??` / `T!!` / `T!?` (and `T!` whose `T` is an error kind), and
calling `panic(...)` — user code never spells an abort: only the runtime
aborts (`main`'s unwrap failure, `as*` / `peek` / `writeAt` past the end,
close failure, and `out.*`'s write failure).
`swap(a, i, i)`
is identity — the checker elides the self-swap move trio instead of
vacating the slot. No
lifetime inference, no alias analysis.

### Semantics that touch ownership

- `match x` consumes x (bindings move out); `match &x` inspects via views.
- Unwrap is a consume: `x?` / `x!` / `??` move owned payloads out; a
  view-unwrap (`&x?`) binds a const view of the payload — the container
  inspection pattern (`HASH_MAP.md`).
- `loop e in arr` binds a view (via `current &T`).
- Closures capture by value (copy const handles, move owned values); they own
  their environment and may escape. A callback registered asynchronously runs
  as another coroutine: owned moves and frozen shares cross in; a view in its
  captures is a compile error.
- Channels: sending an owned mutable value moves it; const handles are shared.
  Suspended coroutines keep their arena chain alive.
- clib("m"): C receives a raw `*T` borrow; the caller's arena must outlive the
  call; C must not retain the pointer after return.
- Asynchronous registrations (epoll / kqueue / io_uring completions) retain
  their buffer past the call: they are crossing sites, so they take an
  ownership move or a pool-promotion — never a view.

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
`bool`, `char`, `byte` — checked per instantiation), `Mutex[T]`, and
`chan[T]`. See
`## Threads and synchronization` below.

## Threads and synchronization

`Atomic[T]`, `Mutex[T]`, and `chan[T]` are the only shared mutable state (see
`### Thread boundary` above). They are intrinsics: **refcounted handles** to
runtime-managed cells — each handle is conceptually a *mutable view of the
cell*, implemented with synchronization under the hood of the language. A
handle crosses a thread boundary by sharing; the cell lives in runtime
memory, never in an arena, so it can never dangle. The cell stays mutable
behind the `const` handle, exactly as `ch <- v` mutates a channel behind its
handle. The deallocation machinery is embedded at the end of the lifetime in
which each handle appeared: a scope-exit decrements the reference count, and
at zero the cell is freed and its payload destroyed.

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

`chan[T]` is a handle to a runtime-managed cell, in the same family as the
atomic and mutex cells. `chan[T].new(n)` creates a channel: `n` slots of
buffer; `chan[T].new(0)` is unbuffered, a rendezvous — send and receive pair
up and transfer the value directly. The handle is a **refcounted pointer**
(see `## Threads and synchronization`): every coroutine holding it co-owns a
*mutable view of the cell*, implemented with synchronization under the hood
of the language. The cell's memory is managed by the reference count — its
deallocation machinery is embedded at the end of the lifetime in which each
handle appeared, and at zero the cell is freed with its payload.

Closing is a runtime operation on the shared cell, not an owner-exclusive
one: **any coroutine holding a handle may close** it with `ch.dispose()`.
The call consumes the calling coroutine's own binding, and a close on an
already-closed channel aborts (double-close). Channel handles are therefore
excused from the static exactly-once gate of `## Resources` — like mutex
handles, unlike `Fd`. A closed-but-referenced cell keeps draining until the
last handle dies; sends to a closed channel abort.

```c
pong func (ch const chan[string]) = {
  loop s := <-ch? do       // receive; absence (closed and drained) ends the loop
    out.println(s)
}

ch chan[string] = chan[string].new(0)  // new(n): n slots; 0 = unbuffered rendezvous

spawn pong(ch)                    // owned handle widens to const: the cell is shared
ch <- "hello, world"              // frozen literal — the pool buffer is shared into the cell
ch.dispose()                      // close: no more sends; pong drains, then sees absence
```

The operators are Go's, spelled on ownership. `ch <- v` *moves* `v` into the
cell — the value relocates to the receiving coroutine's arena. Copyable
values (`int`, `bool`, ...) are copied in, const handles share the cell, and
frozen values (`const string`, pool literals) share their buffer — exactly
like argument passing. A value whose shape contains a view never crosses:
the `### Thread boundary` rule, so no view can reach another coroutine
through a channel. Sending and receiving take the const handle; closing is
allowed from any holder.

`v = <-ch` yields `T?` — the language never fabricates a zero value where
Go's `v, ok := <-ch` would: a value means the receive succeeded, absence
means closed and drained. `loop x in ch` iterates until absence.
`chan[T].new(16)` is
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
      j := <-jobs =>               // fires when a job arrives; j binds the payload
        results <- j * 2
      (<-jobs)? => return          // absence — jobs closed and drained: we are done
      <-cancel => return           // fires when cancel is closed or delivers
    }
  }
}
```

Ready rules are judged on the `### Channels` cell state:
- a receive arm fires on **presence** (`j := <-ch =>`, binding the payload);
  a `(<-ch)?` arm fires on **absence** — once the channel is closed and
  drained;
- send: ready when a buffer slot is free or a receiver is parked.

Every case expression (the channel and value operands) is evaluated exactly
once when the `select` is entered, in source order — expressions are
side-effect-free (assignment is a statement), so evaluating them all is
unobservable. When more than one arm is ready, one is picked uniformly at
random, like Go — a busy channel cannot starve the others. If none is ready,
`default` runs; without `default` the coroutine parks (the same machinery as
`lock()`) until one becomes ready.

A send arm selected on a closed channel aborts, exactly like `ch <- v`
outside `select`. A receive must spell its absence per `### Channels`: after
the channel is closed and drained, `j := <-ch` never fires and `(<-ch)?`
fires — the body must exit. `select` covers
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
    // do work; an abort is process-wide — never just this coroutine
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

`try` guards an operation that returns `T!` — and only `T!`: `T?` has its
own lighter handling — the `?`, `??` and `if ...?` forms of
``## `T?` and `T!` `` — and never enters a guarded scope.

`try` may be applied only to a statement or an expression, never to a block of code.

The `try-catch` pair — from the first `try` to the single `catch` — is one
lifetime, like a code block `{ statements }`.

A guarded scope is the tail of a block:

- the fallible operations stack as `try <statement>` — one statement or
  expression each, never a block, all at the top level of the region;
- the region ends with exactly one `catch <name>` — a **label with a
  parameter**. Every statement after it, to the end of the enclosing block, is
  the handler region: flat, no braces, no extra indent;
- `catch` binds only the error value. The handler sees `e` plus whatever
  Copy/const names the block held before the first `try` — nothing declared
  inside the region. `catch` acts as a label: everything after it is a list
  of statements — the error handler.

```c
readFile func (path const string) = {
  status int = 200

  try file := open(path)              // File! — unwrapped by the guard
  defer file.dispose()                // registered only because the try above succeeded

  try data := file.readAll()          // []byte!
  try use(data)                       // success tail

  // both paths settle here — defers registered above fire now
  catch e

  log("read failed: %s{e}")       // the handler is everything after `catch`,
  status = 500                    // to the end of this block — flat
  fallback(status)
}
```

Semantics:

- **One error kind.** `T!` carries no error parameter: every fallible
  operation reports through the single intrinsic error type, so mixed origins
  in one region need no unification — `catch e` binds whatever the failed op
  produced. (Declared error kinds and `is`-testing: the next subsection.)
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
- **`!` settles inside a region.** A bare `!` anywhere in a guarded scope is
  not an early return — it fails the region to its own `catch`. `?` stays a compile
  error inside a region — absence has no handler — and `??` stays legal
  (``## `T?` and `T!` ``).

### Error kinds, `is`, and binding

Errors are an intrinsic kind — a declared error type carries payload fields:

```c
IOError error = {
  customParam int
  customMessage string
}
// the canonical JsonParseError — fromJson (## Metaprogramming) uses this shape
JsonParseError error = {
  message string
  offset  uint
  cause   error
}
```

One **built-in** kind is public and structurally normal: `Error { message
string, code int }`. Intrinsics fill it from the OS (errno into `code`, a
message for `%s{e}`), and user code may construct it directly; it is always
a possible `cause`.

On failure the failing operation fills a payload and stacks it as the
intrinsic error; the handler binds it (`catch e`) and dispatches by kind:

```c
// readConfig: string! — IOError possible; parseUser: *User! —
// JsonParseError possible (the &-created user lands in the caller's arena, C7)
loadUser func (path const string) = {
  try s := readConfig(path)                // string! — any intrinsic error
  try u := parseUser(s)                    // *User!
  authorize(u)                             // success tail
  catch e
  if e is IOError io then
    out.println("failed to read user: %s{io.customMessage}")
  else if e is JsonParseError jp then
    out.println("failed to parse user: %s{jp.message}")
  // kinds without a test fall through — handled implicitly by the handler's end
}
```

- **`is` is deep.** `e is IOError io` tests the error's *dynamic kind* —
  type identity — along the **`cause` spine**, outermost first; the first
  match wins (Go's `errors.Is` walk).
- **The find binds a new name.** A true `is` binds the found member to the
  name you give it; `e` itself is untouched and remains the caught, top
  error — owned, printable, returnable, wrappable as a whole. Reading a
  payload through *unbound* `e` is a compile error; the checker verifies
  binding with the same per-branch machinery `match` uses. `==` between
  error values is a compile error — every "is this the error I care about"
  is answered by `is`; a payload-less kind (`NotFound error = {}`) is a
  sentinel, tested the same way.
- **Bound names are views.** The named member is a read-only view into the
  chain: payload fields print, never move — `return io` is a compile error
  (a nested value cannot be moved out of its wrapper, R4). An error chain
  is therefore **append-only**: the only transform is wrapping the top —
  `return SocketError { message = "accept: %s{e}", cause = e }` — and a
  found member can never be re-contextualized into a new wrapper.
- **Aggregates are opaque.** A kind may carry `causes []error` — legal
  payload (errors are non-disposable, so the slice is too) — but the deep
  walk follows only the single `cause` spine; reaching the children is
  explicit iteration (`loop c in v.causes`), and a bare `e is IOError` never
  fires on a child that sits only inside an aggregate.
- **Non-disposable payloads.** An error type may name only non-disposable
  field types — the declaration itself is rejected otherwise. Errors never
  carry owned resources, so the failure-path rule (dispose before `Err`) is
  untouched, and an *unread* error needs no discharge: plain dealloc
  machinery at the end of `e`'s scope (R1).
- **Checkable, not exhaustive.** Handlers are the deliberate Go-style
  relaxation of `match` exhaustiveness: a new error kind compiles everywhere
  and falls through until a test is added. The handler's end is the implicit
  catch-all.


## `T?` and `T!`

Absence and fallibility are **postfix type shapes**: `T?` is the
maybe-value (`Optional[T]` under the hood), `T!` the fallible value
(`Result[T]` under the hood, carrying the single intrinsic error). The
checker's shapes are `{ Some(T), None }` and a success/failure pair — but
users never spell `Some` / `None` / `Ok` / `Error`: absence and failure are
handled **by form, not by arms**, so `match` on a `T?` or `T!` value is a
compile error (`## Pattern matching`). Stacking is banned too (`T??`,
`T!!`, `T!?` are compile errors), and `T` in `T!` must not itself be an
error kind — a bare `return v` could not tell a success from a failure
otherwise.

**Writing the shapes.** In a `T!` function a failure is `return <error-kind
value>` and the success is `return v` — the compiler wraps by the declared
return type (`return NumberError { … }` is a failure, `return r` a success,
`return f()` with `f : V!` passes through, and `return e` re-raises the
intrinsic error from a handler). In a `T?` function `return v` wraps the
success and a bare `return` returns **absence**. Assignment to a `T?`
target wraps the same way: a declared `T?` — local or field — defaults to
absent, `x = v` wraps, `x = {}` clears. The implicit tail return wraps like
`return`.

**Reading the shapes is obligated-unwrap** — the checker rejects any bare
use of a `T?` as its `T` (arithmetic, passing to a `T` parameter, assigning
to a `T`), with one exception: the absence tests `x == {}` / `x != {}`.
`T!` is readable only through `!` or `try`. The unwrap forms, in expression
position:

- `expr!` — `T!` only. On error the enclosing function returns the
  intrinsic error — or the whole process panics in `main` — otherwise the
  expression evaluates to the payload. Inside a guarded scope `!` is not an
  early return: it fails the region to its own `catch` (`## Try / catch`).
- `expr?` — `T?` only. On absence the function returns (bare) — or panics
  in `main` — otherwise the expression evaluates to the payload. In a
  conditional **head** (`if x := expr?`, `loop x := expr?`) `?` instead
  *branches*: the payload binds for the block, absence runs the `else`
  (`### If statement`).
- `expr ?? default` — `T?` only. On absence the expression evaluates to
  `default` and the function keeps going. There is never a return, so `??`
  works in any function, `main` included, and inside guarded scopes.

**Unwrap is a consume.** `!`, `?`, and `??` move the payload out of the
shape; a non-Copy payload is moved, so a box cannot be unwrapped twice.
Unwrapping through a *view* (`&x?`) binds a **const view of the payload**
instead — the container inspection pattern (`HASH_MAP.md`): the map reads a
slot's `T?` entry through a view without ever taking ownership.

```c
truncateRead func (f *File, n int) string! = {
  buffer bytes = {}
  s := f.readLine(&buffer)!       // writable view of the caller's scratch buffer
  return string.from(s[:n])       // auto-wrap: success
}

getUserAuthorities func (login const string) []string? = {
  aths := repository.selectAuthoritiesForUser(login)?
  return aths.filter(s != "")     // auto-wrap: success
}

greet func (login const string) string = {
  name := repository.nickname(login) ?? login   // absent: keep going with login
  return "hello, " + name
}
```

Rules:

- **Forced return types.** A bare `!` forces its function to return `T!`; a
  bare `?` forces `T?`. The two cannot coexist in one function — they force
  incompatible return types — while `??` forces nothing and mixes freely.
  `main` is exempt from the forcing: its failure path is an abort — the
  runtime reports the intrinsic error and terminates, never a return. The
  checked conditional head and `try` force nothing.
- **Payloads.** `?` returns absence, which carries nothing, so a `Y?` can
  feed a function returning any `X?`. `!` returns the intrinsic error value
  — a kind-tagged payload and the one and only error type — so the
  expression's error and the function's error match by construction.
  Success payloads are unconstrained.
- **Guarded scopes.** Inside a `## Try / catch` region a bare `!` is not an
  early return — it fails the region to its own `catch`; a bare `?` is a
  compile error (absence has no handler); `??` has no failure path, so it
  stays legal.


## Generics

`[T]` as *parameters* appears only on the declaration side, after the kind
word — `func [T]`, `struct [T]`, constraint forms like `struct [E
Iterable]`, `interface [T[E, _]]`, and specializations like `func
[array[E]]` above. Instantiation puts *arguments* on the referenced name:
`*Head[T]`, `B?`, `Iterable[T] interface`. Calls never repeat type
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
x = int.from("1234")!   // `int` is an ordinary argument (a type value), not instantiation
```

```c
// E must implement `Iterable` — see the marker below
MyStruct struct [E Iterable] = {
  x E
}

// T is a container, its first generic is the element type
Iterable interface [T[E, _]] = {
  begin   func(c *T) Iterator[E]
  end     func(c *T) Iterator[E]
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

// -- emit failures are *values*, not types: a non-finite float, or the depth
// -- cap. The parse kind `JsonParseError` is declared in ## Error kinds
JsonWriteError error = { message string; cause error }

// toJson walks obj through const views only — reflection never takes, moves,
// mutates, or disposes. `O` is constrained **JSON-shaped** at instantiation
// (scalars, string, enum, array, struct — no `*T`, `any`, or disposable
// fields), so serialization cannot cycle: there are no references to follow.
// The output is arena-built in the caller's statement-block arena (C7) and
// dies with the caller's block.
toJson func [O] (obj const *O, n := 0) string! = {
  if n > 64 then
    return JsonWriteError { message = "too deeply nested" }
  indents := "    " * n
  json := indents + match O {
    String(s) => "%q{s}\n"                       // s: const view of the value
    Integer(i) => "%n{i}\n"                      // scalars arrive Copy
    Float(f) => if f.isFinite() then "%f{f}\n"
                else return JsonWriteError { message = "non-finite float" }
    Boolean(b) => "%b{b}\n"
    Enum(e) => "%q{e.name}: {\n" + toJson(e.value(), n + 1)! + "\n}\n"  // deep failures propagate
    Array(a) =>
      subjson string
      loop i, e in a {                           // i: iteration ordinal = slot for arrays
        mayBeComma := if i + 1 < a.length then "," else ""
        subjson += indents + "%s{toJson(e, n + 1)!}%s{mayBeComma}\n"
      }
      "[\n" + subjson + indents + "]\n"
    Struct(s) =>
      subjson string
      loop i, f in s.fields {                    // s.fields yields const *field — read-only views
        mayBeComma := if i + 1 < s.fields.length then "," else ""
        subjson += indents + "%q{f.name()}: %s{toJson(f.value(obj), n + 1)!}%s{mayBeComma}\n"
      }
      "{\n" + subjson + indents + "}\n"
  }
  return json
}

// -- parse: genuinely fallible — malformed input is data (T!), unlike the
// -- invariant panics above. Success **&-creates the whole O graph** in the
// -- caller's statement-block arena and returns a view into it: survives the
// -- call, bulk-freed at the caller's block exit, no dispose (the newList
// -- precedent, C7). The same JSON-shaped constraint applies: a parser cannot
// -- construct `*T`/`any`/disposable fields from text.
fromJson func [O] (json const string) *O! = {
  parser := JsonParser { input = json, at = 0 }       // JsonParser: intrinsic
  try obj := parser.parse[O]()                        // nested &-creates land in
  return obj                                          // the caller's arena (C7)
  catch e
  return JsonParseError { message = "json parse failed", offset = parser.at, cause = e }
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
  runtime("basic") // implicitly imported, gives: runtime.out, runtime.log, runtime.process, memory allocator, regexp parser... compile-time.
  lib("fmt", "sync") // regular library, standard or custom, compile-time
  git("git:github.io/username/reponame.git") // pull from the git repository
  source("./libs/source-file.lang") // the language source file, compile-time
  clib("m") // library compiled from C language, compile-time
}
```

## Modules and globals

- A file may declare several modules with a **prefix declaration** — `module
  name` — and every following top-level definition belongs to it until the
  next `module`.
- Names and code defined outside any function are **global** (module-wide).
  Executable top-level statements are packed into a synthesized
  `module_initializer` per module; at assembly the compiler collects every
  module initializer and calls them from the **initializer section** of the
  main module — the very first thing a binary runs. They execute in
  **module definition order**, so an imported module's initializer that was
  included earlier runs first (dependencies before dependents).
- A global is visible **only across a direct import edge**: module `B`'s
  globals are visible in `A` exactly when `A` imports `B` — never
  reversed, never transitively.
- `#compiler.private` on a declaration removes the name from the
  **link-visible set**: an importing module cannot reference it (a compile
  error). `#compiler.inline()` is the other declaration directive.

### Output: `out` aborts, `log` reports

The convenience print family — `out.println`, `out.print`, `out.error` —
**aborts on write failure** (a runtime abort — one more backstop in the
C20 list; user code never spells a panic). Code that must survive an output
failure uses the same names from `runtime.log`: `log.println(data const
string) uint!` (likewise `log.print`, `log.error`) — `T!`, read through
`!` or `try`.

## Socket server

A TCP echo server, exercising `T!` for syscall failures, exhaustive
`match`, `&` move-in for single-owner sockets, method sugar, `dispose` for
OS resources (`## Resources` below), `spawn` — the coroutine operator
(used like Go's `go`), and channels (`### Channels`). Only `socket.*`
intrinsics (from `clib("c")`) are sketched beyond the core language.

```c
// A TCP echo server: accept forever, echo each received line back, close.
// OS failures are data — T!, not exceptions; user code never calls panic.

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

// -- errors are declared kinds (see ## Try / catch); `cause` chains the
// -- underlying failure so `is` tests see through the whole stack
SocketError error = {
  message string
  cause  error
}

// -- bind + listen
newListener func (address const string, port uint) Listener! = {
  try fd := socket.listen(address, port)   // Fd! — unwrapped by the guard
  return Listener { fd = fd }              // auto-wrap: success
  catch e
  return SocketError { message = "cannot listen on %s{address}:%d{port}: %s{e}", cause = e }
}

// -- accept one connection; failures here are transient, the caller keeps serving
accept func (listener *Listener) Connection! = {
  try fd := socket.accept(listener.fd)   // fd through a view: *Fd
  peer := socket.peerName(fd)            // read fd first — then move it into the field
  return Connection { fd = fd, peer = peer }
  catch e
  return SocketError { message = "accept: %s{e}", cause = e }
}

// -- slurp one line (until \n, or EOF with data)
readLine func (conn *Connection) string! = {
  buf string
  try loop {
    ch := socket.recv(conn.fd)!       // view: conn is *Connection; fails to the catch
    if ch == '\n' then
      return buf                      // auto-wrap: success
    buf += string.from(ch)!
  }
  catch e
  if buf.length > 0 then
    return buf               // EOF with data: deliver what we have
  return SocketError { message = "connection closed: %s{e}", cause = e }
}

write func (conn *Connection, data const string) uint! = {
  socket.send(conn.fd, data)           // uint! — tail return passes through
}

// -- `Connection.dispose` / `Listener.dispose` are synthesized from the fd
// -- field; `dispose func (f &Fd)` above is the only leaf body. Call sites:
// -- `conn.dispose()` in echo, `l.dispose()` in main.

// -- one coroutine per connection; `&` moves ownership in
echo func (conn &Connection) = {
  peer := conn.peer                          // const string — value binding
  defer conn.dispose()                       // fires on both paths
  try text := conn.readLine()
  try n    := conn.write(text)
  out.println("echoed %d{n} bytes to %s{peer}")
  catch e
  out.println("to %s{peer}: %s{e}")
}

serve func (listener *Listener) = {
  loop {
    try conn := listener.accept()        // Connection!
    spawn echo(&conn)                    // the fd's ownership moves into the coroutine
    catch e
    out.println("%s{e}")
  }
}

main func () = {
  cfg := ServerConfig { address = "0.0.0.0", port = 8080 }
  l := newListener(cfg.address, cfg.port)!   // main is exempt: failure aborts the process
  serve(&l)                // serve borrows a view; we still own the listener
  l.dispose()
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

## Bytes

`bytes` is a builtin mutable byte buffer — packed bytes plus a read cursor,
all operations intrinsics. It is arena memory, like `[]T`: it never needs
`dispose`. There are no fixed-width integer types — widths live in the buffer
API, so `asU16()` reads two bytes and returns a `uint`. `byte` exists as an
8-bit Copyable scalar (`b[i]`, `0x0A`).

```c
b bytes                      // empty, ready
b bytes = {1, 2, 3, 4}       // cursor at 0
b += more                    // append at the end, like string +
s := b.string()              // utf-8 copy of the contents (binary-safe)
b2 := bytes.from("Hello")!  // buffer from a string — `from` is fallible
```

Reads consume from the cursor; writes *append* at the end and never clobber
the unread tail — to re-read a frame you built, `reset()` first.

- Reads: `asU8()` → `byte`, `asU16()` / `asU32()` / `asU64()` → `uint`,
  `asI8/16/32/64()` → `int`, `asF32()` / `asF64()` → `float`,
  `asStr(n)` → `string` (a copy).
- Writes: `writeU8/16/32/64`, `writeI8/16/32/64`, `writeF32/64`, `writeStr(s)`
  — append and grow; a value truncates to its width (wrap semantics).
- Absolute, cursor-free: `asU16At(pos)`, `writeU32At(pos, v)`, … — patch in
  place; a write past the end zero-fills the gap; the cursor stays put.
- Lookahead: `peek(n)` — a view of the next n bytes, cursor untouched.
- Delimited: `asLine()` / `asUntil(delim)` — consume through the delimiter
  and return what preceded it (delimiter not included); when the buffer ends
  first they return the remainder and `eof()` flips.

Cursor and meta: `reset()` → cursor 0, contents kept · `pos(n)` absolute ·
`skip(n)` · `remaining()` · `eof()` (remaining == 0) · `length`.

Endianness defaults to hardware native; a per-buffer override switches it for
portable files and protocols: `b.endian(Endian.big)` / `b.endian(Endian.little)`.

Bounds: an `as*` / `peek` / `writeAt` past the end **panics** — a programmer
bug, not a `T!`. `recvExact` guarantees lengths at the I/O boundary, so a
framed read never runs past its frame. A *short* read — fewer bytes than
requested — is data, reported with the shared `IOError` vocabulary —
`ShortRead` is a declared error kind (`IOError error = {…}`, see
`## Try / catch`); EOF is *not* an error — a receive on a drained channel/socket
yields absence, per `### Channels`.

Slicing — views, not copies: `b[2..<5]` is a write-through view (a mutation
through it hits the buffer), `.copy()` for owned data, `b[i]` reads one
`byte`. A view is valid until the buffer grows again. This is the framing
pattern: read the length prefix, slice the body, parse the slice.

Transforms — produce a new buffer: `b.or(0xFF)` / `b.and(0x0F)` mask each
byte with `mask & 0xFF`; `b.xor(key)` is element-wise over the key bytes (a
shorter key cycles); `b.not()` flips every bit. Byte arithmetic wraps modulo
256; `0x` / `0b` literals are available.

Manual codecs are the recommended shape — a cursor makes them trivial:

```c
sendRequest func (conn *Connection, req Request) uint! = {
  body bytes
  body.writeU16(req.kind)
  body.writeU16(req.count)
  frame bytes
  frame.writeU32(0)                  // length-prefix placeholder
  frame += body
  frame.writeU32At(0, body.length)   // patch it in place — cursor-free
  socket.send(conn.fd, frame)        // uint! — tail return passes through
}

readRequest func (conn *Connection) Request! = {
  hdr  := socket.recvExact(conn.fd, 4)!   // failure propagates
  n    := hdr.asU32()
  body := socket.recvExact(conn.fd, n)!
  Request { kind = body.asU16(), count = body.asU16() }   // tail: auto-wrap
}
```

The reflection-driven whole-struct binary codec — `toBytes` / `fromBytes` —
is `BINARY_CODEC.md` (C17): the C12 shape, binary instead of text — one wire
format, fixed-width, `Endian.big`, length-prefixed names and counts. Manual
codecs stay the norm when the wire is externally specified or framed
per-packet; `readRequest` above is that style, without reflection.

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

**Failure-path disposal.** A function returning a failure or absence must have
discharged every owned disposable it still holds first — open → read → fail
⇒ close before returning the failure.

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
