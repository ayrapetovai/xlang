#pragma once

#include <stdio.h>
#include "tokens.h"

/*
 * Bootstrap parser — a hand-written recursive-descent parser for
 * GRAMMAR.md, producing a uniform AST in an arena.
 *
 * The lexer is driven once to EOF into a token array first: that gives the
 * parser arbitrary lookahead for the grammar's structural ambiguities
 * (typed calls `f[T](…)` vs index `a[i]`, conformance markers
 * `Iterable[T] interface` vs array fields `x [10]int`, func-vs-variable
 * dispatch, and the contextual `{` rewrite rules) and cheap backtracking
 * via token-index save/restore.
 *
 * Node model: every node is a tag + one payload token + a child array
 * (`ch`). Lists (argument lists, param lists, statement lists, bodies) are
 * `N_LIST` nodes holding their items in `ch`. All memory comes from the
 * parser's arena, so failed backtracking attempts simply leak into the
 * arena and are never visited again.
 */

typedef struct Node Node;

typedef enum {
  /* --- structure ------------------------------------------------------ */
  N_FILE,        /* top-level items: ch = nodes                            */
  N_LIST,        /* generic sep-separated list                             */
  N_MODULE,      /* ch[0] = name                                           */
  N_IMPORT,      /* ch[0] = kind name, ch[1] = N_LIST of string literals   */
  N_DIRECTIVE,   /* ch[0] = name, ch[1] = member?, ch[2] = args?(N_LIST)   */

  /* --- declarations ---------------------------------------------------- */
  N_VAR,         /* ch[0] name, ch[1] type?, ch[2] init?, ch[3] directive ;*/
                 /* tok = `:=` for the define form                           */
  N_STRUCT_DECL, /* ch[0] name, ch[1] typeparams?, ch[2] body?(N_LIST) /   */
                 /* do-stmt / directive                                     */
  N_ENUM_DECL,   /* ch[0] name, ch[1] body?(N_LIST) | do-stmt              */
  N_ERROR_DECL,  /* ch[0] name, ch[1] body?(N_LIST)                        */
  N_INTERFACE_DECL, /* ch[0] name, ch[1] typeparams?, ch[2] body(N_LIST)   */
  N_FUNC_DECL,   /* ch[0] qname(N_NAME|N_QNAME), ch[1] typeparams?,        */
                 /* ch[2] params(N_LIST), ch[3] ret?, ch[4] dirs?(N_LIST), */
                 /* ch[5] body?(N_BLOCK|N_EXPR|stmt)                       */
  N_QNAME,       /* ch[0] first, ch[1] second  — atomic.new                 */
  N_PARAM,       /* ch[0] name?, ch[1] type, ch[2] default?;               */
                 /* tok = `:=` for deduced+default; `const` via text? — use */
                 /* tok.kind == T_KW_CONST to mark const                    */
  N_TYPEPARAM,   /* ch[0] constraint? (N_NAME|"struct"|typeargs); tok=name  */
  N_TYPEARGS,    /* N_LIST of N_TYPEARG                                      */
  N_TYPEARG,     /* ch[0] type? (null = `_` wildcard)                       */
  N_FIELD,       /* ch[0] name, ch[1] type, ch[2] default?, ch[3] dirs?    */
  N_METHOD,      /* ch[0] name, ch[1] typeparams?, ch[2] params(N_LIST),   */
                 /* ch[3] ret?, ch[4] directive?                            */
  N_ENUM_MEMBER, /* ch[0] name, ch[1] type? | params(N_LIST) | enum body,  */
                 /* ch[2] default?                                          */
  N_CONFORM,     /* ch[0] name, ch[1] typeargs?  — Iterable[T] interface    */

  /* --- types ------------------------------------------------------------ */
  N_TFUND,       /* tok = the fundamental keyword                            */
  N_TNAMED,      /* ch[0] name, ch[1] typeargs?                              */
  N_TARRAY,      /* ch[0] size?, ch[1] elem                                  */
  N_TVIEW,       /* ch[0] base; tok = `*` or `&`                             */
  N_TFUNC,       /* ch[0] params(N_LIST), ch[1] ret?                         */
  N_TCONST,      /* ch[0] base                                               */
  N_TSHAPE,      /* ch[0] base; tok = `?` or `!`                             */

  /* --- literals / initializers ------------------------------------------- */
  N_INIT,        /* ch = N_INITITEM elements                                 */
  N_INITITEM,    /* ch[0] name?, ch[1] expr | nested N_INIT                   */
  N_STRUCT_LIT,  /* ch[0] name, ch[1] typeargs?, ch[2] N_INIT                */
  N_ANON_STRUCT, /* ch[0] fields?(N_LIST)                                    */
  N_FUNC_EXPR,   /* ch[0] params(N_LIST), ch[1] ret?, ch[2] block            */
  N_TRAILING,    /* ch[0] expr  (form 1)  |  ch[0] params(N_LIST) ch[1] expr  */

  /* --- statements -------------------------------------------------------- */
  N_BLOCK,       /* ch = statements (a block is an arena: one lifetime)      */
  N_IF,          /* ch[0] cond, ch[1] then, ch[2] else?                      */
  N_CHECK_IF,    /* ch[0] name?, ch[1] cond, ch[2] then?, ch[3] else?        */
  N_LOOP,        /* ch[0] clause/block, ch[1] body?/until?                   */
  N_LABEL,       /* ch[0] label name, ch[1] the labeled statement — outer: loop */
  N_CFOR,        /* ch[0] name, ch[1] init, ch[2] cond, ch[3] post?, ch[4] body */
  N_IN_CLAUSE,   /* ch[0] ordinal?, ch[1] name, ch[2] expr, ch[3] body       */
  N_CHECK_IN,    /* ch[0] name, ch[1] expr, ch[2] body                       */
  N_MATCH,       /* ch[0] expr, ch[1] arms(N_LIST)                           */
  N_MATCH_ARM,   /* ch[0] patterns(N_LIST), ch[1] body                       */
  N_SELECT,      /* ch[0] arms(N_LIST)                                       */
  N_SELECT_ARM,  /* ch[0] head, ch[1] body                                   */
  N_TRY,         /* ch[0] guarded statement                                  */
  N_REGION,      /* ch[0] region stmts(N_LIST), ch[1] catch name?,           */
                 /* ch[2] handlers?(N_LIST)                                  */
  N_RETURN,      /* ch[0] expr?                                              */
  N_DEFER,       /* ch[0] statement or block                                 */
  N_TRANSFER,    /* ch[0] label?; tok = break/continue/yield                 */
  N_SPAWN,       /* ch[0] postfix expr                                       */
  N_SEND,        /* ch[0] lhs, ch[1] rhs  — ch <- v                         */
  N_SWAP,        /* ch[0] a, ch[1] b — a <> b                               */
  N_ASSIGN,      /* ch[0] lvalue, ch[1] rhs; tok = `=` or compound op;      */
                 /* tok = `:=` for select presence heads (name := <-ch)      */
  N_EXPR_STMT,   /* ch[0] expr                                               */

  /* --- expressions -------------------------------------------------------- */
  N_BIN,         /* ch[0] l, ch[1] r; tok = the operator (incl. range ops,   */
                 /* `??`, `&&`, `|`, `&`, shifts, comparisons …)             */
  N_UNARY,       /* ch[0] operand; tok = the op                              */
  N_RECV,        /* ch[0] operand — <-ch                                     */
  N_INDEX,       /* ch[0] base, ch[1] accessor expr (range allowed)          */
  N_SLICE,       /* ch[0] base, ch[1] lo?, ch[2] hi?                         */
  N_CALL,        /* ch[0] callee, ch[1] typeargs?, ch[2] args?(N_LIST)       */
  N_MEMBER,      /* ch[0] base, ch[1] name                                   */
  N_UNWRAP,      /* ch[0] base; tok = `!` or `?` (or view-unwrap `(e)?`)      */
  N_NAME,        /* tok = the identifier; `_` is a N_NAME whose text is "_"  */
  N_LIT,         /* tok = numeric / char / string / true / false literal     */
  N_IS,          /* ch[0] expr, ch[1] name, ch[2] name?  — e is IOError io   */

  N_KIND_COUNT
} NodeKind;

struct Node {
  NodeKind k;
  int line, col;
  Token tok;         /* payload leaf / operator                             */
  Node **ch;         /* arena-allocated child pointers                      */
  int n;             /* live child count                                    */
  int cap;           /* allocated capacity (for growth)                     */
};

/* --- arena -------------------------------------------------------------- */
typedef struct Arena {
  void **blocks;     /* every allocation is reachable from here            */
  size_t nblocks, cap;
  char *cur, *end;   /* current bump region                                */
} Arena;

void *arena_alloc(Arena *a, size_t n);
void arena_free(Arena *a);

/* --- parser ------------------------------------------------------------- */
typedef struct Parser {
  Lexer lx;
  Token *toks;       /* whole-file token stream                             */
  size_t n, cap;     /* token count / capacity                              */
  size_t cur;        /* read cursor                                         */
  Arena ar;
  int failed;        /* sticky: first error wins                            */
  int quiet;         /* backtrack attempts suppress error recording         */
  int no_q;          /* suppress postfix `?` (checked heads)                */
  int in_head;       /* suppress Name "{" struct literals (checked heads) */
  char msg[512];     /* diagnostic                                         */
  Node *root;
} Parser;

/* Runs the whole pipeline up to the AST. Returns 0 and sets *out on
 * success; returns -1 with p->msg set otherwise (lexer errors included). */
int parser_run(Parser *p, const char *src, Node **out);

const char *node_kind_name(NodeKind k);
void node_dump(const Node *n, int depth, FILE *out);