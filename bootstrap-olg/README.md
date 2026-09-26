# Bootstrap — CLI and the interpreter

The bootstrap is the C program that parses and interprets the language
(decision C24): it lexes a whole source file, parses it to the uniform
AST, and executes it by walking the tree. It runs the self-hosted target
compiler — written in the language itself — and the example witnesses.
Performance is irrelevant; fidelity to the AST and rubber-stamping the
memory-safety bar are not.

## Build

    make

Produces `./bootstrap` (`cc -std=c17 -Wall -Wextra -Werror -O0 -g
main.c lexer.c tokens.c parser.c exec.c -lm`).

## Usage

    usage: bootstrap [lex|ast|run] <file>

With a single argument (`./bootstrap file.lang`) the mode defaults to
`lex`.

| Mode  | What it does                                          | Exit codes                      |
|-------|--------------------------------------------------------|--------------------------------|
| `lex` | Token dump: one `kind  line:col  text` line per token  | 0 ok, 1 lexical error          |
| `ast` | Parse the whole file and dump the AST (`node_dump`)    | 0 ok, 1 parse error            |
| `run` | Parse **and interpret**: executes the program (`main`) | 0 ok, 255 on a fatal diagnostic|

A fatal diagnostic is printed as
`bootstrap: <file>:<line>:<col>: <message>` (parse errors and runtime
errors alike); `run` maps it to shell exit 255 (`exec_run` returns -1).

## Running a program

A runnable file is a module with a `main func () int`; its returned value
is the process exit status.

```lang
module hello

main func () int = {
  who := "artem"
  out.println("hello, %s{who}!")     // string interpolation (C26)
  loop i in 0..<5 do
    out.println("%d{i}")             // format specs evaluate per iteration
  0
}
```

    ./bootstrap run hello.lang
    # hello, artem!
    # 0
    # 1
    # 2
    # 3
    # 4

## String interpolation and escapes (GRAMMAR §2.3, decision C26)

The lexer keeps each `"…"` literal as one raw token; the interpreter
processes the body at eval time:

- **Escapes** (minimal set): `\n \t \r \0 \\ \" \' \xNN` — an unknown
  escape is a clean diagnostic.
- **Format specs**: `"…%L{expr}…"` where `expr` is any expression, parsed
  with the same parser and evaluated immediately.
  - `%s` — value as text (mirrors `string.from` / `println`)
  - `%q` — quoted, JSON-ish (`"` `\` `\n` `\t` `\r` `\0` escaped)
  - `%d` / `%n` — decimal (int/uint/byte/char)
  - `%f` — float (`%g`), strictly float values
  - `%b` — `true`/`false`, strictly bool values
- A bare `%` and unknown letters stay literal (the letter set is open).
- A literal with neither `\` nor `%` is returned as-is (source slice) —
  `examples/exec.lang` is the byte-identical regression baseline.

## Scope limits (exec.h, decisions C18/C24)

Diagnosed as "not executable in the bootstrap phase", not run:
channels (send/recv/select), `spawn`, `yield`, generic instantiation,
slot moves / views / `&`.

## Example witnesses

| File              | What it shows                                    |
|-------------------|--------------------------------------------------|
| `examples/exec.lang`   | interpreter regression baseline (literal strings, byte-identical output) |
| `examples/hello.lang`  | warm-up; now prints `hello, artem!` via `%s{}`  |
| `examples/interp.lang` | string-interpolation witness (all format letters, escapes, caught errors, loop specs) |

`examples/README.md` covers the lexer-level fixtures (`types`,
`expressions`, `control`) and the public lexer API.

## Debug builds

The audit (decision C25) verifies every path with sanitizers and valgrind:

    cc -std=c17 -Wall -Wextra -Werror -O0 -g -fsanitize=address,undefined \
       -fno-sanitize-recover=all -o /tmp/opencode/boot_asan \
       main.c lexer.c tokens.c parser.c exec.c -lm
    valgrind --leak-check=full --error-exitcode=99 ./bootstrap run examples/interp.lang