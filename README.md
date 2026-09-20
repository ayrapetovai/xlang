# Abstract

Must be written in C language to embeddable and to interoperate with native code.
Commas ',' are separators as '\n' and ';'.
Pointer decay.
Pattern matching.
All pointers are non-null.
Strings and arrays with length.
No shadowing, redefinition instead.
Variables are mutable by default.
Each variable, func parameter of field can be const.
Statements are expressions.
Strings can be concatenated and multiplicated like in python.
Data types: `void`, `byte`, `char`,  `int`, `float`, `bool`, `string`, `struct`, arrays, `enum`.
Meta types: `type`, `func`, `field`, `pointer`, `value`, `any`.
Function's return type counts for signature.
Function's return type participates in overload resolution.
Only explicit casts allowed.
For unused variables use '_'.
Channels and coroutins, like in Go language.
Operators can be overloaded.
No exceptions but stacktraces.
Panic is not recoverable, it destroys the whole application (with stack rollback).
The `match` (aka `switch`) is strictly exhaustive.
Dynamic types dispatching for interfaces, like in Go.
String interpolation with formatting.
Any value can be written to `ByteBuffer` witch is suitable everywhere.
Meta-type information is stored in the binary. Types are never erased.
Memory ownership: reference (variable) owns memory, pointer does not.

# Syntax Examples

## Commentaries

```c

// single line comment

/*
multi line comment
*/

/**
  multi line comment
  a : int
  /*
    inner multi line comment
  */
  b : 10
**/
```

The closing literal complements by amount of stars.

## Variable declaration

```c
x : int // default value, x == 0
x : int 42
x : 42 // omit type if inferable
x : const float 3.14

s : string // default value s == ""
s : "abc"
s : string "Hello, World!"

s : string.from(1) // s have type string and is "1"
unsignedVar : uint.from(-1) // unsignedVar is 1
```

## Array declaration

```c
a : [10]int
a : [size]int
a : []int
a : []int {1, 2, 3}
a : []int {
  1
  2
  3
}
r : []int 0..=5 // r is {0, 1, 2, 3, 4, 5}
```

## Pointers

```c
s : "Hello"                             // s : string = "Hello"
p : &s                                  // p : *string = &s; p points to s
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
infix_operator== : func (a : string, b : string) #compiler.inline()
do
  if a.length != b.length then
    return false
  else loop i in 0..<a.length do
    if a@(i) != b@(i) then
      return false
  true
```

## Control structures

### If statement

```c
b : bool true

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

i : 10
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

loop i : 0; i < 10 do
  oneLineStatement(i++)

loop i : 0; i < 10; i += 1 do
  oneLineStatement()

loop i < 10 do
  oneLineStatement(i++)

a : []int{1, 2, 3}
loop x in a do
  oneLineStatement(x)

// this `in` plays only in context of `loop`
loop i in 0..<a.length do
  oneLineStatement(a@(i))

outer: loop do
  loop {
    x : random(10)
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
begin : func [T](c : T) Iterator[T]
end : func [T](c : T) Iterator[T]
next : func [T](it : Iterator[T]) Iterator[T]
current : func [T](it : Iterator[T]) &T

Iterator: struct [T : struct ] {
  data : T
  index : int 
}
// intrinsic array definition
array : struct [T] {
  values : []T
  length : uint
}
begin : func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data : &array
    index : 0
  }

end : func [array[E]] (a *array[E]) Iterator[E] do
  Iterator {
    data : &a
    index : a.length
  }
next : func [array[E]] (it : Iterator[array]) Iterator[E] do
  Iterator {
    data: it.data
    index: it.index + 1
  }
current : func [T] (it : Iterator[T]) &T do
  it.data@(it.index)

// so user can do
ar : []int {1, 2, 3, 4}
loop it : begin(ar); it != end(ar); it = next(ar) do
  out.println(current(it))
// by this
loop e in ar do
  out.println(e)
```

## Data declarations

### Structures

```c
User : struct {
  id : int
  password : string #access.private()
}

AccountNumber : struct of value : const string

Account : struct {
  owner : User
  account: const AccountNumber  // set once at construction
  createdAt: const Date         // set once at construction
}

acc : Account {
  owner : { // placing `User` between : and { is optional
    id : generate()
    password : authentication.genPass()
  }
  account : { "111111111" }
  createdAt : date.now()
}
Permanent : const struct {      // only const instances can be created
  x : const 42                  // const with default value, can't be set at construction
}
p : Permanent // all fields are const, the default value will have them
```

### Enumeration declaration

```c
MyBeInt : enum { // implicitly has field with type descriptor
  Somting : int 1
  Empty
}
// in standard library
Optional : const enum [T any] {
  Some(T)
  None
}
```

## Pattern matching

The result of the last calculated statement is returned by `match`.

```c
MyEnum : enum { A, B }
e1 : MyEnum.A
match e1 {
  A => out.println("A")
  B => out.println("B")
}

TheEnum : enum { A(int), B(string) }
e2 : TheEnum
match e2 {
  A(x) => out.println("e2's value is %d{x}")
  B(s) => out.println("e2's value is %s{s}")
}

SubEnum : enum {
  ONE(enum {
    INNER_1(int)
    INNER_2(string)
  })
  TWO
}

se1 : SubEnum.ONE(INNER_1(42))
se2 : SubEnum.ONE(INNER_2("Hello"))
se3 : SubEnum.TWO

enums : []SubEnum { se1, se2, se3 }

loop e in enums {
  match e {
    ONE(INNER_1(i)) => out.println("integer i = %d{i}")
    ONE(INNER_2(s)) => out.println("string i = %s{s}")
    TWO => out.println("nothing to print")
  }
}

fieldNames : match e1.type {
  Struct(s) => s.fields(e1).map(toString).join(", ")
  Enum(e)   => e.enumerators(e1).map(toString).join(", ")
  _ => ""
}

x : random(10)
y : 7
match x {                                // each comma separated expression must be true
  1, 2, 3       => out.println("x in [1, 2, 3], and is %d{x}")
  x % 2 == 0    => out.println("x is even") // applied if the first pattern fails
  x < y, x > 2  => out.println("x is less than y and greater than 2") // same
  _             => out.println("nothing paticular")
}
```

## Function declaration

```javascript
foo : func (x : int) int {
  x * x // single statement in the root of func block -> return
}

bar : func (x : int) int do
  oneLineStatement(x, 1)

// method
bark : func (d *Dog, times : int) do loop times do out.println("woof")

foo : func (prompt : string) bool {
  name : string
  in.scanf(prompt, &name)
  if name.length == 0 then
    return false
  name.authenticate()
}
```

### Default values for function parameters

```c
foo : func (x : int, y : int 0) {}
// foo : func (x: int, z : int) {}   // clashes by signature with first foo, compile error
foo : func (x: float, z : int) {} // does not clashes by signature with foo for declaration

foo(1)      // ok
foo(1, 2)   // ok
foo(1.0, 1) // does not clash with first foo for calling

foo : func(x : int, y : int) int { 1 }
a : foo()         // a is int, only one of foo return a value, others return `void`, variable cannot be `void`.
a : float foo() // error, no explicit cast
```

### Function calls

```c
a : []int {1, 2, 3}
fold(a, func (a, b) { a * b }) // implicit return is single expression, types inferred
// call with trailing block, parameter names for lambda from declaration of `fold`
fold(a) {
  a * b
}
fold(array: a, folder : func (a : int, b : int) int { return a * b }) // explicit
b : foo() // b is initialized by value returned by foo()
b = bar() // b is assigned a value returned by bar()

d : Dog
d.bark(10)
```

### Special Functions

```c
// first argument is type, we have nothing to do with it
// intrinsic function, defined in 'basic' package
from : func (:int, s : string) Result[int] {
  r : 0
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

x : int
x = int.from("1234")
```

## Memory Ownership

Reference (to some variable) owns memory, pointer does not.
Variable declaration allocates memory on stack.
Pointer declaration allocates memory in heap.

```c
{ // code block is a lifetime space
  s1 : struct {}
}
// s1 is destroyed

{
  s2 : & struct {}
}
// s2 is also destroyed
```

Variable can be consumed only once.

```c
foo : func(s : string) do out.println(s)
s : "Hello"
foo(s)       // s is consumed
out.print(s) // compilation time error, s is consumed
```

Consuming semantics:
foo : func (s : &string)  - consumes (takes ownership and destroys).

foo : func (s :  string)  - does not consume (does not destroy), pass by value.
foo : func (s : *string)  - does not consume, modifications visible for the caller.

foo : func () :  string  - pass ownership to the caller, pass by value, copied.
foo : func () : *string  - does not not pass ownership to the caller, pass by reference, no copy.


## Generics and Templates

```c
// E must have methods of `Iterable` in scope
MyStruct : struct [E : Iterable] {
  x : E
}

// T is any type, the first generic of T must be the element type
Iterable : template [T[E, _]] {
  begin   : func(c : T) Iterator[E]
  end     : func(c : T) Iterator[E]
  next    : func(it : Iterator[E]) Iterator[E]
  current : func(it : Iterator[E]) &E
}

MyArray[T] : struct Iterable[T] {
  // ...
}
// MyArray requires functions for Iterable to be in scope.

// any could be defined like this, but it is intrinsic
any : struct [T] {
  type : Type
  value : *T
}
```

## Metaprogramming

```c
// Here `json` is a package, name and ignore are functions,
// that take `type` or `field` as arguments. Functions mutate metadata of fields
// Thy are called by the compiler, before compiling dependent functions.
User : struct {
  id       : int      #json.ignored()
  name     : string   #json.name("username")
  birth    : Date     #json.name("dateOfBirth")
  password : string   #json.masked(json.mask.first(10))
}

// this json serializer does not pay attention to field metadata
toJson : func [O] (obj *O, n : 0) Result[string] {
  indents : "    " * n
  json : indents + match O {
    String(s) => "%q{s}\n"
    Integer(i) => "%n{i}\n"
    Float(f) => "%f{f}\n"
    Boolean(b) => "%b{b}\n"
    Enum(e) => "%q{e.name + "_" + e.type}: {\n" + toJson(e.value(), n + 1) + "\n}\n"
    Array(a) =>
      subjson : string
      loop e in a {
        mayBeComma : if !last then "," else ""
        subjson += indents + "%s{toJson(e, n + 1)}%s{mayBeComma}\n"
      }
      "[\n" + subjson + indents + "]\n"
    Sturct(s) =>
      subjson : string
      loop f in s.fields {
        mayBeComma : if !last then "," else ""
        subjosn += indents + "%q{f.name()}: %s{toJson(f.value(x), n + 1)}%s{mayBeComma}\n"
      }
      "{\n" + subjson + indents + "}\n"
  }
  return Ok(json)
}
```

```c
a : int 1
b : int 2
assertTrue(a != b)            // intrinsic infix_operator!=: func(a : int, b : int) bool
assertTrue(a.type == b.type)  // intrinsic infix_operator==: func(a : type, b : type) bool
```

### Type descriptors

```c
type : enum {
  Struct(struct)
  Enum()
  Func()
  Int()
  Bool()
  Float()
  Array()
}

struct : struct {
  name : string
  package : string
  generics : []generic
  macros : []macro
  fields : []field
  layout : layout
}

field : struct {
  name : string
  type : type
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


