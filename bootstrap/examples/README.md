# Examples

All examples are **lexer-level**: they demonstrate tokenization only — the
parser (and semantic checks) are a later increment, and nothing here is
type-checked. The sources use only syntax witnessed in `README.md` /
`GRAMMAR.md`, so they stay valid once the parser lands.

## Build the bootstrap lexer once

    make

## Token dump — how the lexer sees a source file

    ./bootstrap examples/hello.lang

Prints one line per token: `kind  line:col  text`. Works on any of the
`.lang` files; exit code 0 on success, 1 on a lexical error.

## Example files

| File | What it shows |
|------|---------------|
| `hello.lang` | warm-up — `module`, `#import`, the three comment forms, struct/enum/error declarations, string interpolation literals |
| `types.lang` | every type form of GRAMMAR.md §5 (fundamental, array, view, named generics, function types, `T?`/`T!` shapes) plus §2.3 literals and §3.2 directives; the contextual meta-type words `type`/`field`/`value` used as names |
| `expressions.lang` | the full operator set of §2.4 (shifts, cyclic shifts, `<>`, ranges, `<-`, `=>`, compound assignments), line continuation (`.`, `+`, `=`, …), unwrap/fallback (`!`, `?`, `??`) and an `infix_operator==` declaration |
| `control.lang` | statements of §6 — `if/then/else`, `loop` forms (in, ordinal, C-style, checked `:=` … `?`, `until`), the descending range `s.length>..=0` (open item §9 — it lexes as `> ..=`), `match` arms with `=>`, `select`, and the `try … catch` guarded region |

## Using the lexer API from your own C code

`lexer_demo.c` is a small consumer: it loops `lexer_next()` until `T_EOF`,
slices token text via `start`/`len`, detects member accesses (an identifier
right after a `.` token), prints every string literal, and reports per-kind
counts via `token_kind_name()`.

    cc -std=c17 -Wall -Wextra -Werror -I. -o lexer_demo lexer_demo.c lexer.c tokens.c
    ./lexer_demo examples/control.lang

The public surface is just three symbols (`lexer.h`, `tokens.h`):

    void  lexer_init(Lexer *l, const char *src);
    Token lexer_next(Lexer *l);        /* T_EOF, T_UNDEF (see Lexer.err), or a token */
    const char *token_kind_name(TokKind kind);