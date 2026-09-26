# Grammar — Extended Backus–Naur form for the language

This is the constituent grammar of the language as specified by
`README.md` (the normative spec) and the decisions recorded in
`OWNERSHIP_DRAFT.md` / `OWNERSHIP_RULES.md`. It is **derived from the
examples and prose of the spec**, not from an implementation, so where a
form appears only once or is not spelled out at all, the rule is marked as
an **open item** instead of being invented.

Conformance status: the enumerations and comments reference the
`OWNERSHIP_RULES.md` / `OWNERSHIP_DRAFT.md` decisions (C11–C23) and
`README.md` sections (`## …`).

---

## 1. Notation

EBNF metasymbols:

```
::=        definition
|          alternation
X?         zero or one X
X*         zero or more X
X+         one or more X
( ... )    grouping
`tok`      terminal — a keyword or punctuation token
Name       nonterminal
NAME       terminal class described in the lexical sections
-- text    comment on a rule
```

- Terminals are written in backticks: `` `if` ``, `` `{` ``, `` `..=` ``.
  Because the language itself uses `[ ] { } ( ) |` as tokens, they *always*
  appear inside backticks; the metasymbols above are the only meta use of
  `? * + ( )`.
- A bare `Name` in a rule is a nonterminal (an identifier production); a
  bare capital word such as `Expression` is likewise a nonterminal.
- **Universal separator rule.** Newline, `;`, and `,` are interchangeable
  separators (`README #### Abstract`: "Commas ',' are separators as '\n'
  and ';'"). Every list production (`ArgumentList`, `FieldList`,
  `PatternList`, `StatementList`, …) takes the separator
  `Sep = NL | ";" | ","` between elements, with a trailing `Sep` allowed.
  The rules below therefore write lists as `X { Sep X }` and omit the
  `Sep` in the prose.
- **Line continuation.** A source line that *begins with an operator
  symbol* (`` `.` `+` `&&` `==` `->` `,` `` …) continues the statement
  started on the previous line (`README ## Line continuation`). This is a
  lexical rule: the newline separator is suppressed in that position.

---

## 2. Lexical grammar

### 2.1 Comments

```
Comment
  = LineComment
  | BlockComment

LineComment = "//" { Char } NL

BlockComment                -- nested; depth 1 shown (README ## Commentaries)
  = "/*" { BlockChar } "*/"
  | "/**" { BlockChar } "**/"
BlockChar = Char | BlockComment
```

Block comments may nest. The opening run of `*` (one or more) is
"complemented" by the closing run: a comment that opens as `/**` closes as
`**/` (`README ## Commentaries`: "The closing literal complements by amount
of stars"). The exact minimum closing-star rule for nested mixed runs is an
open item (only the `/*`…`*/` and `/**`…`**/` forms are witnessed).

### 2.2 Names, keywords, reserved words

```
Identifier
  = Letter { Letter | Digit | "_" }        -- no shadowing; `_` is reserved as
                                           -- the anonymous name / wildcard
```

Reserved words (cannot be declared or referenced as plain identifiers):

```
if      then    else    loop    do      until
match   select  default         try     catch
defer   spawn   return  break   continue  yield
in      is      const   module  import
type    field   value   pointer any     func
struct  enum    interface       error
void    byte    char    int     uint    float   bool    string  bytes
true    false
```

Notes:

- `struct`, `enum`, `func`, `interface` are **explicitly reserved**
  (`README ## Metaprogramming`: the descriptor "is named `structDesc`"
  because `struct`, `enum`, `func`, `interface` are reserved). `type` and
  `field` are meta-type words but *are* redeclared in the spec
  (`type enum = {…}`, `field struct = {…}`, `field type type`), as are
  `value`, `pointer`, `any` (`value type`, `value *T`) — meta-type words
  may appear as declaration and member names (§4).
- Keywords of the language proper (`if`, `loop`, `match`, `return`, …)
  cannot be names.
- `error` is the built-in error type and the kind word of error
  declarations; `string`, `bytes`, numeric words are type names, not
  declaration names.
- `main`, `dispose`, `take`, `swap`, `from`, `new`, `load`, `store` are
  ordinary names of builtin/intrinsic functions, not keywords.
- `it` is the reserved binding name of a single-parameter trailing block
  (`## 5.6 Expressions`).
- Operator declarations are named by composition: `infix_operator==`,
  `infix_operator<`, `infix_operator!=`, … (§4.6).

### 2.3 Literals

```
Literal
  = IntegerLiteral | FloatLiteral | CharLiteral | StringLiteral
  | "true" | "false" | "{" "}"        -- `{}` is the empty initializer /
                                      -- absence default (a "literal")

IntegerLiteral
  = DecLit | HexLit | BinLit
DecLit   = Digit { [ "_" ] Digit }        -- 1, 42, 1_000 — `_` only between digits
HexLit   = "0x" HexDigit { [ "_" ] HexDigit }   -- 0x0A, 0xFF, 0xAB_CD (## Bytes)
BinLit   = "0b" ( "0" | "1" ) { [ "_" ] ( "0" | "1" ) }   -- (## Bytes)

FloatLiteral = Digits "." Digits [ Exp ]
             | Digits Exp                 -- 3.14, 1.0, 1e3, 1.5e-3 — exponent floats;
                                          -- `1.` stays int + `.` (no digit follows)
Digits = Digit { [ "_" ] Digit }
Exp    = ( "e" | "E" ) [ "+" | "-" ] Digits

CharLiteral = "'" CharLiteralBody "'"        -- '0', '9', '-', '\n', '\'' ?
CharLiteralBody = Char | Escape

StringLiteral = "\"" { StringBody } "\""
StringBody    = Char | Escape | FormatSpec
Escape        = "\\" EscapeChar              -- \n \t \\ \" \0 … (open set)
FormatSpec    = "%" FormatLetter "{" Expr "}" -- %s{e} %d{i} %q{s} %n{i}
                                              -- %f{f} %b{b} — the letter set is
                                              -- open (## Abstract: "String
                                              -- interpolation with formatting")
```

- `int` literals have no sign; `-1` is unary minus (`## Operators`).
- Digit separators `_` and exponent floats are lexed: dec `1_000`, `1e3`,
  `1.5e-3`, hex `0xAB_CD`, bin `0b1010_1010` (user ruling, decision C23).
- The format-letter set witnessed is `s d n q f b`; the complete set and
  width/precision forms are **open items** (out of the bootstrap compiler's
  scope).
- A bare `%` inside a string (escaping / literal percent) is an **open
  item** — no use is witnessed.

### 2.4 Operator and punctuation tokens

```
-  swap                `<>`
-  address-of / move   `&`        (&x : address-of; &T : move-in, §5)
-  arithmetic          `+` `-` `*` `/` `%`
-  shifts              `<<` `>>` `>>>` and cyclic `<<~` `>>~`
-  logic               `&&` `||` `!` `<` `>` `<=` `>=` `==` `!=`
-  bitwise             `&` `|` `~` `^`
-  unwrap              `!` `?` `??`            (postfix / fallback)
-  envelope            `(` `)`  `[` `]`  `{` `}`  `.` `,` `;` `:`
-  assignment          `=` and the compound family (see below)
-  channel             `<-`
-  ranges / slices     `..<`  `..=`  `>..=`  `>..<`   (descending forms are the
                       adjacent token pair `>` + `..=`/`..<`, bound as one
                       left-associative range operator — §7; `>` keeps its
                       relational role when an operand follows)
-  match / select arm  `=>`
-  match/select ops    `in`  `is`  `default`
-  parameter default   `name = value`      (named literal fields / args)
```

Compound assignment: `+=` `-=` `*=` are witnessed (`i += 1`,
`list.length -= 1`, `width *= 2`); the rest of the family (`/=`, `%=`,
`<<=`, `>>=`, `&=`, `|=`, `^=`) follows from `README ## Operators`
("Assignment: `=` … use `i += 1`") and is marked as the **family** — only
`+=  -=  *=` appear in examples.

`->` appears in the abstract's continuation list (`README #### Abstract`)
but no role for it is witnessed elsewhere: **open item**.

### 2.5 Separators

```
Sep = NL | ";" | ","
```

---

## 3. Compilation unit

```
CompilationUnit = { TopLevel } EOF

TopLevel
  = ImportBlock
  | ModuleDecl
  | Declaration
  | Statement            -- top-level executable code is packed into the
                         -- module's synthesized `module_initializer`
                         -- (README ## Modules and globals; §12)
```

```
ModuleDecl = "module" Name
```
`module Name` is a **prefix declaration** (`README ## Modules and globals`,
C22): every following top-level definition belongs to it until the next
`module`; a file may declare several.

### 3.1 Imports

```
ImportBlock = "#import" "{" { ImportItem } "}"

ImportItem  = ImportKind "(" StringLiteral { Sep StringLiteral } ")"
ImportKind  = "runtime" | "lib" | "git" | "source" | "clib"     -- witnessed
```
Witnessed `runtime("basic")`, `lib("fmt", "sync")`, `git("<url>")`,
`source("<path>")`, `clib("m")`. Whether `ImportKind` takes arbitrary names
(third-party kinds) is an open item.

### 3.2 Directives

```
Directive
  = "#" Name [ "." Name ] [ "(" [ ArgumentList ] ")" ]
```
Witnessed: `#compiler.inline()` (intrinsic marker on operator/type
declarations), `#compiler.private` (removes the name from the link-visible
set, C22), `#json.ignored()`, `#json.name("username")`,
`#json.masked(json.mask.first(10))`, `#access.private()`.

Attach positions (see the productions): after a field declaration, after a
function's signature (before its body), and on top-level declarations
`#compiler.private` (§4). The exact set of allowed directive names and
their positions is checked by the compiler, not by the grammar.

---

## 4. Declarations

```
Declaration
  = VariableDecl
  | StructDecl
  | EnumDecl
  | ErrorDecl
  | InterfaceDecl
  | FuncDecl
```

### 4.1 VariableDecl

```
VariableDecl
  = Name [ "const" ] Type [ "=" Initializer ] [ Directive ]
  | Name ":=" Initializer [ Directive ]
```

- `x int` — default value (`x == 0`); `x int = 42`; `x := 42` (deduced);
  `x const float = 3.14`.
- `const` sits between the name and the type; the whole *declaration*
  grammar follows `name type` with an optional `= value`
  (`README ## Syntax Examples`).
- `_` may be the name (unused binding): `loop _ in 0..<i do`.

`Initializer = Expression` — any value expression, including the elided
`{ … }` literal (§7); `buf []T = {}` uses the empty initializer.

### 4.2 StructDecl

```
StructDecl
  = Name [ "const" ] "struct" [ TypeParams ]
      ( "=" StructBody | "do" Statement | Directive )
```
`Name const struct = {…}` is the const-instances-only form
(`README ## Data declarations`): `Permanent const struct = { x const int = 42 }`.

```
StructBody = "{" { StructMember } "}"

StructMember
  = FieldDecl
  | ConformanceMarker

FieldDecl = Name [ "const" ] Type [ "=" Expression ] [ Directive* ]

ConformanceMarker
  = Name [ TypeArgs ] "interface"     -- "Iterable[T] interface" (§12);
                                      -- not a member: no layout
```

### 4.3 EnumDecl

```
EnumDecl   = Name "enum" [ "=" EnumBody | "do" Statement ]
EnumBody   = "{" { EnumMember } "}"

EnumMember
  = Name [ Type ] [ "=" Expression ]            -- Somting int = 1 ; Empty ;
                                                -- Accepted ; TWO
  | Name "(" EnumParams ")"                     -- A(int) ; B(string)
  | Name "(" "enum" "=" EnumBody ")"            -- ONE(enum = { INNER_1(int) … })

EnumParams = ParameterList     -- payload fields, named or bare types
```

### 4.4 ErrorDecl

```
ErrorDecl = Name "error" ( "=" ErrorBody | "{" "}" )
ErrorBody = "{" { FieldDecl } "}"               -- error kinds are struct-shaped
                                                -- with restricted payload types
                                                -- (non-disposable, §12)
```
Witnessed `IOError error = {…}`, `NumberError error = { message string }`,
`SocketError error = { message string, cause error }`,
`JsonWriteError error = { message string; cause error }`,
`NotFound error = {}`, `Error { message string, code int }` (builtin).

### 4.5 InterfaceDecl

```
InterfaceDecl = Name "interface" [ TypeParams ] "=" InterfaceBody
InterfaceBody = "{" { MethodSig } "}"
MethodSig     = Name "func" [ TypeParams ] "(" [ ParameterList ] ")" [ Type ] [ Directive ]
```
A method signature binds no implementation; the interface always carries an
implicit `dispose` member (`README ## Resources`). `interface` as a *type*
is non-ownable-shape checked by the checker, not the grammar.

### 4.6 FuncDecl

```
FuncDecl
  = QualifiedName "func" [ TypeParams ]
      "(" [ ParameterList ] ")"
      [ Type ]                     -- return type; `T?` / `T!` shapes allowed
      [ Directive* ]               -- e.g. #compiler.inline() before `do`
      [ FuncBody ]                 -- absent for intrinsic/imported funcs
                                   -- (newList, lock, atomic.load, …)

QualifiedName = Name [ "." Name ]
                -- dotted declarations are static constructors:
                -- `atomic.new func [T] (init T) Atomic[T]`,
                -- `mutex.new func [T] (v T) Mutex[T]` (README ## Threads)

FuncBody
  = "=" Block
  | "=" Expression                 -- single-expression body: `= { 1 }`
  | "do" Statement

OperatorName = "infix_operator" BinaryOperatorWord
```
The operator name is the literal composed token; `infix_operator <op>` appears
in place of `QualifiedName` in the declaration: `infix_operator== func (a
const *string, b const *string) bool = {…}`.

`dispose func (v &T) = {…}` is an ordinary `FuncDecl` whose name is
`dispose` — it is recognized by shape (the `&T` parameter), which makes `T`
disposable (`README ## Resources`). No special production.

### 4.7 ParameterList

```
ParameterList = Parameter { Sep Parameter }

Parameter
  = [ Name ] [ "const" ] Type [ "=" Expression ]   -- name optional (bare
                                                    -- type: `from func (int, …)`;
                                                    -- const before type)
  | Name ":=" Expression                             -- deduced type + default:
                                                     -- `toJson (obj const *O, n := 0)`
```
A trailing single-parameter binding name is `it` and belongs to trailing
blocks, not to `ParameterList`.

### 4.8 TypeParams

```
TypeParams = "[" TypeParam { Sep TypeParam } "]"

TypeParam
  = ParamName [ Constraint ]        -- T ; E Iterable ; T struct
  | ParamName [ TypeArgs ]          -- array[E] ; T[E, _]
Constraint  = "struct" | Name [ TypeArgs ]
ParamName   = Name | "_"
```
Witnessed: `func [T]`, `struct [E Iterable]`, `interface [T[E, _]]`,
`func [array[E]]`, `struct [T struct]`, `Iterator struct [T struct]`,
`struct [T]`, `any struct [T]`, `func [T] (…)`, `newList func [T] ()`.

---

## 5. Types

```
Type
  = ConstType
  | ShapeType
  | PlainType

ConstType = "const" PlainType          -- const T, const *T, const string,
                                       -- const Atomic[T], const []T
                                       -- (shared / read-only)

ShapeType = PlainType ( "?" | "!" )    -- T? optional, T! fallible;
                                       -- *O!, uint!, []byte!, chan[int]?
                                       -- stacking (T??, T!!) is a compile
                                       -- error (## `T?` and `T!`)

PlainType
  = FundamentalType
  | ArrayType
  | ViewType
  | NamedType
  | FunctionType

FundamentalType
  = "void" | "byte" | "char" | "int" | "uint" | "float" | "bool"
  | "string" | "bytes" | "error" | "any"
  | MetaType
MetaType  = "type" | "func" | "field" | "pointer" | "value"
           -- ## Abstract: meta types

ArrayType = "[" [ Expression ] "]" Type         -- [10]int, [size]int, []int,
                                                -- []string, []SubEnum
ViewType  = "*" Type | "&" Type                 -- *T writable view;
                                                -- const *T read-only view
                                                -- (&T : move-in parameter,
                                                -- non-owning view return)

NamedType = Name [ TypeArgs ]                   -- *Head[T], Atomic[uint],
                                                -- Mutex[Counter], chan[string],
                                                -- Iterable[T], array[E],
                                                -- Iterator[array], Locked[T]
TypeArgs  = "[" TypeArg { Sep TypeArg } "]"
TypeArg   = Type | "_"                          -- T[E, _] wildcard

FunctionType = "func" "(" [ FunctionTypeParams ] ")" [ Type ]
FunctionTypeParams = Type { Sep Type }          -- function types in parameter
                                                -- lists carry no names
```

Notes on `&` position (README «Copyable types»): before a parameter type =
move-in; before an expression = address-of; in a return type = non-owning
view. The grammar is position-insensitive; the checker enforces the rule.

`?` / `!` also appear *postfix on expressions* (`PostfixExpr "?"` /
`PostfixExpr "!"`) and in checked heads (`CheckedIf`, `CheckedInClause`) —
those are `## 7` / §6.3 / §6.4, not types.

---

## 6. Statements

```
Statement
  = VariableDecl { Sep }                       -- declarations are statements
  | AssignmentStmt
  | SendStmt
  | SwapStmt
  | ExpressionStmt
  | IfStmt
  | LoopStmt
  | MatchStmt
  | SelectStmt
  | GuardedRegion                               -- try … catch (C21)
  | ReturnStmt
  | DeferStmt
  | TransferStmt                                 -- break / continue / yield
  | SpawnStmt
  | Block

ExpressionStmt = Expression

Block = "{" { Statement } "}"                    -- a block is an arena: one
                                                 -- lifetime (README «Memory»)
```

### 6.1 Assignment

```
AssignmentStmt
  = LValue "=" Expression                        -- `=` yields no value; no
                                                 -- chained `a = b = c`
  | LValue CompoundAssignOp Expression
  | LValue "<>" LValue                           -- swap

CompoundAssignOp
  = "+=" | "-=" | "*=" | "/=" | "%="
  | "<<=" | ">>=" | ">>>="
  | "&=" | "|=" | "^="

LValue
  = Name
  | PostfixExpr                                  -- a[i], p.x, g.value.total,
                                                 -- a[i][j]; `a[i] = v` can also
                                                 -- be a move-in reinit (§12)
```

### 6.2 Channel send / receive

```
SendStmt = Expression "<-" Expression            -- ch <- v ; results <- j * 2

ReceiveExpr = "<-" UnaryExpr                     -- yields T? ; `v = <-ch`,
                                                -- `(<-jobs)?`, select arms
                                                -- (definition repeated in §7)
```

### 6.3 If

```
IfStmt
  = "if" Expression "then" Statement [ "else" Statement ]
  | "if" Expression Block [ "else" ( Statement | Block ) ]
  | CheckedIf
  | IfExpr

CheckedIf                             -- the checked head: `?` branches,
  = "if" Name ":=" Expression "?" [ "then" Statement ]
        [ "else" Statement ]          -- absence runs else; then optional
                                      -- (`if y := m.get(other)? else …`)
  | "if" Expression "?" "then" Statement [ "else" Statement ]
                                      -- smart-cast on a named T?
```

The `then`-branch is exactly one statement (`README ### If statement`);
`else` binds to the nearest `then` (dangling-else). The checked binding
scopes to the branch block; `else` optional on either side.

### 6.4 Loop

```
LoopStmt
  = [ Label ":" ] "loop" LoopClause

Label = Name                             -- outer: loop do …

LoopClause
  = Block [ "until" Expression ]         -- loop { } / loop { … } until cond
  | Expression "do" Statement            -- loop i < 10 do s ; loop !stop.load() do ;
                                         -- numeric-range head: loop 0..<10 do s
                                         -- (a range is an Expression)
  | Expression Block                     -- loop cond { … }
  | CForLoopClause
  | InClause
  | CheckedInClause

CForLoopClause
  = Name ":=" Expression ";" Expression [ ";" Expression ] Body
                                        -- loop i := 0; i < 10 { … }
                                        -- loop i := 0; i < 10; i += 1 do s
                                        -- loop it := begin(&ar); it !=
                                        --   end(&ar); it = next(it) do s
Body = "do" Statement | Block

InClause = [ Ordinal "," ] NameOrWildcard "in" Expression Body
Ordinal  = Name                          -- loop i, x in ar (i = ordinal)
NameOrWildcard = Name | "_"

CheckedInClause = Name ":=" Expression "?" Body
                                        -- loop s := <-ch? do ;
                                        -- loop b := q.poll()? do
```

`in` plays only inside `loop` (`README ## loop with in`); iterating a
channel (`loop x in ch`) yields per-element values and ends on absence.

### 6.5 Match

```
MatchStmt = "match" Expression "{" { MatchArm } "}"

MatchArm = PatternList "=>" ArmBody

PatternList = Pattern { Sep Pattern }      -- comma-separated: each must hold
                                           -- `1, 2, 3 =>` , `x < y, x > 2 =>`

ArmBody
  = Block
  | Statement                              -- single statement / expression
  | { Statement }                          -- unbraced list: the arm value is
                                           -- the last statement's value; the
                                           -- list ends where an arm head
                                           -- starts in a following line
                                           -- (toJson's `Array(a) => …`)
```

`match` is strictly exhaustive; `match` on a `T?`/`T!` value is a compile
error (handled by form, not arms). `match` may be an *expression* — the
result of the last statement is its value (`README ## Pattern matching`).

### 6.6 Patterns

```
Pattern
  = Wildcard                              -- `_`
  | Literal                               -- 1, 2.5, "s", true, { }
  | GuardExpr                             -- any expression: x % 2 == 0
  | ConstructorPattern

ConstructorPattern
  = Name                                  -- Accepted, B, TWO, Struct, String
  | Name "(" PatternList ")"              -- Rejected(reason), A(x),
                                          -- ONE(INNER_1(i)), Struct(s)
```
Constructor patterns cover enum variants and kind tests
`ONE(INNER_1(i))`, `A(x)`. Type-descriptor arms (`String(s)`, `Integer(i)`,
`Enum(e)`, `Array(a)`, `Struct(s)`, `Float(f)`, `Boolean(b)`) are ordinary
name patterns of the intrinsic `type` descriptor — no special rule.

### 6.7 Select

```
SelectStmt = "select" "{" { SelectArm } "}"

SelectArm = SelectHead "=>" ArmBody

SelectHead
  = "default"                              -- `default` escape
  | ReceiveExpr                            -- `<-cancel`
  | "(" ReceiveExpr ")" "?"                -- `(<-jobs)?` — absence arm
  | Name ":=" ReceiveExpr                  -- `j := <-jobs` — presence arm
  | SendStmt                               -- `results <- j * 2`
```

An empty `select` is a compile error (never testified in the grammar); arm
expressions are evaluated once on entry.

### 6.8 try / catch — the guarded region (C21)

```
GuardedRegion
  = RegionStmt { RegionStmt } "catch" Name { Statement }

RegionStmt = "try" Statement  -- try-guarded: on failure jumps to catch
           | Statement        -- plain: runs only on the success path
                              -- (`defer` between trys, README ## Try / catch)
```
- `try` may be applied only to a **statement or an expression**, *never to
  a block* (C21, `README ## Try / catch`, §12). In the grammar the operand
  is a `Statement`; an expression operand is an `ExpressionStmt`.
- The region is the tail of the enclosing block: the statements run flat
  at the block's top level — some `try`-guarded, some plain — and the
  handler region is everything after `catch <name>` to the end of the
  block, flat and unbraced (`catch` is a label with one parameter).
- At most one `catch` per block; trys do not nest.

### 6.9 Return / defer / transfer / spawn

```
ReturnStmt   = "return" [ Expression ]      -- bare return = T? absence
                                            -- (## `T?` and `T!`)

DeferStmt    = "defer" ( Statement | Block )

TransferStmt = ( "break" | "continue" | "yield" ) [ Label ]

SpawnStmt    = "spawn" PostfixExpr          -- spawn echo(&conn) : a coroutine
                                            -- operator, returns void
```

Deferred bodies return nothing and must be infallible; a defer is
registered by execution (README ## defer).

---

## 7. Expressions

Precedence, loosest to tightest:

```
 01  ??                       fallback unwrap         expr ?? default
 02  ||                       logical or
 03  &&                       logical and
 04  |                        bitwise or
 05  ^                        bitwise xor
 06  &                        bitwise and
 07  ==  !=                   equality (also string ==, error == is a CE)
 08  <  >  <=  >=             relational  (also string ordering)
 09  ..<  ..=  >..=  >..<     ranges (see below)
 10  <<  >>  >>>  <<~  >>~    shifts
 11  +  -                     additive (+ also string concat)
 12  *  /  %                  multiplicative (* also string duplication)
 13  unary  -  !  ~  &  <-    negation, not, bitnot, address-of, receive
 14  postfix  ( )  [ ]  .  :  call, index/slice, member, method sugar
              !  ?            unwrap;  ( )  ?  view-unwrap
 15  primary
```

The spec pins no precedence table; the ladder above is **conventional** and
marked non-normative (§7 bullets) — it matches every witnessed compound
expression (`p == &s`, `i + 1 < a.length`, `1 + 2`, `a.fetchAdd(1) == 0`,
`-1 * r`, `mid + 1 ..= hi`, `off + 4 ..< off + 4 + len`).

```
Expression   = FallbackExpr | IsExpr | InitializerLiteral
FallbackExpr = OrExpr [ "??" OrExpr ]
OrExpr       = AndExpr { "||" AndExpr }
AndExpr      = BitOrExpr { "&&" BitOrExpr }
BitOrExpr    = BitXorExpr { "|" BitXorExpr }
BitXorExpr   = BitAndExpr { "^" BitAndExpr }
BitAndExpr   = EqualityExpr { "&" EqualityExpr }
EqualityExpr = RelationalExpr { ("==" | "!=") RelationalExpr }
RelationalExpr = RangeExpr { ("<" | ">" | "<=" | ">=") RangeExpr }
RangeExpr    = ShiftExpr { RangeOp ShiftExpr }
RangeOp      = "..<" | "..="                       -- ascending
             | ">" "..<" | ">" "..="               -- descending: the token pair
                                                   -- `>` `..<` / `>` `..=`,
                                                   -- adjacent in the stream,
                                                   -- bound as one operator;
                                                   -- left-associative
                                                   -- (`loop c in s.length>..=0`,
                                                   -- `1 >..< 0`)
ShiftExpr    = AdditiveExpr { ("<<" | ">>" | ">>>" | "<<~" | ">>~") AdditiveExpr }
AdditiveExpr = MultiplicativeExpr { ("+" | "-") MultiplicativeExpr }
MultiplicativeExpr = UnaryExpr { ("*" | "/" | "%") UnaryExpr }
UnaryExpr    = ("-" | "!" | "~" | "&") UnaryExpr | ReceiveExpr | PostfixExpr
ReceiveExpr  = "<-" UnaryExpr
PostfixExpr  =
    PrimaryExpr
  | PostfixExpr "(" [ ArgumentList ] ")"
  | PostfixExpr [ TypeArgs ] "(" [ ArgumentList ] ")"   -- parser.parse[O]()
  | PostfixExpr "[" Accessor "]"
  | PostfixExpr "." Name
  | PostfixExpr "." Name [ TypeArgs ] "(" [ ArgumentList ] ")"
  | PostfixExpr "!"                              -- T! unwrap (fails the region
                                                 -- or returns / main aborts)
  | PostfixExpr "?"                              -- T? unwrap (checked heads
                                                 -- branch instead)
  | "(" Expression ")" "?"                       -- view unwrap (&slot.entry)?
                                                 -- (`(&x)?` binds a const view)

Accessor  = Expression                        -- a[i]
          | [ Expression ] ":" [ Expression ] -- s[:n], [:], a[:]
          | RangeExpr                         -- b[2..<5], a[lo..=hi]

PrimaryExpr
  = Literal
  | Name
  | "(" Expression ")"
  | InitializerLiteral                        -- `{1, 2, 3}`, `{}`, `{ id = … }`
  | NamedStructLiteral                        -- Name "{" InitElements "}"
  | AnonymousStructExpr                       -- "struct" [ "{" FieldDecls "}" ] : struct {}
  | FunctionExpr                              -- func (…) { … }
  | MatchExpr                                 -- match e { arms } — primary:
                                              --   indents + match O { … } is legal
  | IfExpr

IfExpr = "if" Expression "then" Expression "else" Expression
         -- expression position (mayBeComma := if … then "," else "")

MatchExpr = "match" Expression "{" { MatchArm } "}"
```

### Initializers and literals

```
InitializerLiteral
  = "{" "}"
  | "{" InitializerElement { Sep InitializerElement } "}"

InitializerElement
  = Expression                                   -- positional
  | Name "=" Expression                          -- named field ("acc { owner = … }")
  | InitializerLiteral                           -- nested elided literal (type
                                                 -- deduced from the field)

NamedStructLiteral = Name "{" InitializerElement { Sep InitializerElement } "}"
                     -- User { … }, Iterator { data = a, index = 0 },
                     -- SocketError { message = "…", cause = e },
                     -- Request { kind = …, count = … }

AnonymousStructExpr = "struct" [ "{" { FieldDecl } "}" ]
                     -- struct {} ; struct { x int }  (README «Where memory
                     -- lives»); `&struct {}` allocates into the arena
```

The **elided** `{ … }` form is only legal where the target type is
inferable from context (field value, named argument, RHS of `:=` of a
declared type) — the disambiguation between `{…}`-initializer and a `Block`
is contextual: `{` in *expression* position is an initializer, `{` in
*statement* position (or as the immediate head of a statement) is a block.
This disambiguation rule is documented, not mechanically enforced by the
EBNF.

Enum construction is ordinary postfix: `MyEnum.A`, `SubEnum.ONE(INNER_1(42))`,
`handlerResult.Rejected("busy")` = `Name "." Name` [ `"(" ArgumentList ")"` ].

### Calls

```
ArgumentList
  = CallArg { Sep CallArg }

CallArg
  = Expression
  | Name "=" Expression                          -- fold(array = a, folder = …)
  | TrailingBlock

TrailingBlock
  = "{" Expression "}"                           -- one parameter binds as `it`
  | "{" Name { Sep Name } ":" Expression "}"     -- { a, b : a * b }
```
A trailing block is a lambda with local parameter names; nothing is
inherited from the callee's declaration.

### Function expressions

```
FunctionExpr = "func" "(" [ LambdaParamList ] ")" [ Type ] Block

LambdaParamList = [ "const" ] Type { Sep [ "const" ] Type }        -- types only
                | Name { Sep Name }                               -- names only
                | ParameterList                                    -- from func (a int, b int)
```
Witnessed: `func (a, b) { a * b }` (names deduced, `=`-free body),
`func (a int, b int) int { return a * b }`,
`func (const T, const T) bool` (function type — §5 `FunctionType`).

### `is`

```
IsExpr = EqualityExpr "is" Name [ Name ]
```
`is` tests an error value's dynamic kind along the `cause` spine and binds
the found member: `e is IOError io` (`README ### Error kinds`; §9). It is
a top-level `Expression` production (spelled where a boolean expression is
expected: `if e is IOError io then …`).

---

## 8. Syntactic constraints carried by the decisions

- **Postfix shapes only.** `T?` / `T!` are postfix type shapes; `T??`,
  `T!!`, `T!?` are compile errors; `match` on a `T?`/`T!` is a compile
  error; `T` in `T!` must not be an error kind (``## `T?` and `T!` ``).
- **Obligated-unwrap.** Reading a `T?` as its `T` is a compile error
  (§12/§8); the reading forms are `expr?`, `expr ?? default`, the checked
  `if/loop …?` heads, `x == {}` / `x != {}`, and view-unwrap `(&x)?`.
  `T!` reads through `expr!` / `try` only.
- **Bare `!` vs `?`.** A bare `!` forces the enclosing function's return
  type to `T!`; within a guarded region it fails to the region's `catch`.
  A bare `?` forces `T?` and is a compile error inside a guarded region;
  `??` forces nothing and is legal everywhere (§8, ``## `T?` and `T!` ``).
  `main` is exempt from the forcing (its failure path aborts).
- **`try` never guards a block** (C21); one flat `catch` per region
  (§6.8).
- **Assignment is a statement** — it yields no value; no `++`/`--`
  (§4.4/§12).
- **`LValue` swaps** (`a <> b`) — the checker elides `swap(a, i, i)`.
- **`match` is exhaustive**; **`select` covers channels only** and an
  empty `select` is a compile error.
- **Top-level code** is collected into `module_initializer` (C22, §3);
  `#compiler.private` names are not link-visible (C22).
- **Module visibility** is across a direct import edge only (C22).
- **Reserved names.** `struct`/`enum`/`func`/`interface` are reserved
  (§2.2); everything else byte-level is checker territory.
- **Casts are `X.from(y)`** (decision C23) — there is **no cast operator**.
  `from`'s first parameter is the target type (a type value); the source
  parameter is an immutable read-only view `const *Y` — **never consumed,
  never modified, no copy** — and the result is always fallible `X!`.
  Predefined for the scalar types (`byte char int uint float bool`) and the
  `string`/`bytes` textual conversions; `from` is an ordinary name —
  user-defined `from`s are allowed (`README ## Special Functions`).

---

## 9. Open items (not invented here)

The grammar records — but does not resolve — the following points:

1. **Operator precedence** is conventional by user ruling (decision C23) —
   §7's ladder stays **non-normative**. The parser still needs a fixed
   order and takes the ladder as written.
2. **Format specifiers** — the letter set (`s d n q f b`) and width/
   precision forms are witnessed-by-example only, and are **out of scope
   for the bootstrap compiler**: formatting is a runtime concern and string
   literals are opaque to the lexer/parser.
3. **Remaining lexical details** — a bare `%` in a string (literal percent)
   and the full escape set (`\n \t \\ \" \0 \'` …) are unspecified.

Resolved by user ruling (decision C23, Sep 26): the **cast operator does
not exist** — casting is the `from` family (`X.from(y)`: first parameter
the target type, source an immutable `const *Y` view, result `X!`; §8) —
so the old `uint(kv.val)` type-name-call witness is rewritten in
`HASH_MAP.md` to `uint.from(kv.val) ?? 0` (the int→uint conversion is
total — the `?? 0` defaults are unreachable); the **descending range** is the
left-associative token pair `>` `..=` / `>` `..<` (§7 — `s.length>..=0`,
and down-to-exclusive `>..<` as ruled); **`->` is ignored** (no production;
lexed as a diagnostic token only, like `++`/`--`); the **compound
assignment family** (`/= %= <<= >>= &= |= ^=`) is accepted; **numeral
lexing** gains digit separators and exponent floats (`1_000`, `1e3`,
`0xAB_CD`); **`interface` bodies are `= { … }` only** — the shared
`= { … } | do …` body rule does not extend to interfaces (§4.5).