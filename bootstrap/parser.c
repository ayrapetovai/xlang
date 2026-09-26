#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokens.h"
#include "lexer.h"
#include "parser.h"

/*
 * Bootstrap parser — hand-written recursive descent over GRAMMAR.md.
 *
 * The whole file is lexed into a token array first (parser.h): that gives
 * arbitrary lookahead for the grammar's structural ambiguities and cheap
 * backtracking via token-index save/restore. `quiet` suppresses error
 * recording during backtrack attempts; failed attempts simply leak into the
 * arena and are never visited again.
 *
 * Grammar points implemented here beyond pure descent:
 *   - the contextual `{` (block at statement position, initializer at
 *     expression position),
 *   - FuncDecl vs variable-of-FunctionType dispatch (params_named scan),
 *   - the composed `infix_operator <op>` name and the descending-range
 *     token pair `>` `..<` / `>` `..=`,
 *   - checked heads (`if …?` / `loop …?`) via the no_q flag,
 *   - the guarded region flattened to the enclosing block's end (§6.8),
 *   - `#import` blocks and `#name[.name][(args)]` directives,
 *   - diagnostic-token rejections: `->` (open §9), `++`, `--`.
 */

#define AR_CHUNK 65536

/* ---------------- arena ---------------- */

void *arena_alloc(Arena *a, size_t n) {
  n = (n + 15u) & ~(size_t)15u;
  if (a->cur == NULL || (size_t)(a->end - a->cur) < n) {
    size_t sz = n > AR_CHUNK ? n : AR_CHUNK;
    char *blk = malloc(sz);
    if (!blk) return NULL;
    if (a->nblocks == a->cap) {
      size_t nc = a->cap ? a->cap * 2 : 16;
      void **nb = realloc(a->blocks, nc * sizeof(void *));
      if (!nb) { free(blk); return NULL; }
      a->blocks = nb;
      a->cap = nc;
    }
    a->blocks[a->nblocks++] = blk;
    a->cur = blk;
    a->end = blk + sz;
  }
  void *r = a->cur;
  a->cur += n;
  return r;
}

void arena_free(Arena *a) {
  for (size_t i = 0; i < a->nblocks; i++) free(a->blocks[i]);
  free(a->blocks);
  a->blocks = NULL;
  a->nblocks = a->cap = 0;
  a->cur = a->end = NULL;
}

/* ---------------- node helpers ---------------- */

static Node *pnode(Parser *p, NodeKind k, Token t) {
  Node *n = arena_alloc(&p->ar, sizeof *n);
  if (!n) return NULL;
  n->k = k;
  n->tok = t;
  n->line = t.line;
  n->col = t.col;
  n->ch = NULL;
  n->n = n->cap = 0;
  return n;
}

/* A fresh list node carrying the current token's position. */
static Token tok(Parser *p, size_t i);          /* defined below */
static Node *pnlist(Parser *p) { return pnode(p, N_LIST, tok(p, 0)); }

static void ppush(Parser *p, Node *list, Node *c) {
  if (!c) return;
  if (list->n == list->cap) {
    int nc = list->cap ? list->cap * 2 : 8;
    Node **nch = arena_alloc(&p->ar, (size_t)nc * sizeof(Node *));
    if (!nch) return;
    if (list->ch) memcpy(nch, list->ch, (size_t)list->n * sizeof(Node *));
    list->ch = nch;
    list->cap = nc;
  }
  list->ch[list->n++] = c;
}

/* A unary node: one child (or an explicit null slot for optional children). */
static Node *punary(Parser *p, NodeKind k, Token t, Node *a) {
  Node *n = pnode(p, k, t);
  if (!n) return NULL;
  ppush(p, n, a);
  return n;
}

/* ---------------- token stream helpers ---------------- */

static Token tok(Parser *p, size_t i) { return p->toks[p->cur + i]; }
static TokKind tk(Parser *p, size_t i) { return p->toks[p->cur + i].kind; }
static int at(Parser *p, TokKind k) { return tk(p, 0) == k; }
static int at_id(Parser *p) { return tk(p, 0) == T_ID; }
static Token curtok(Parser *p) { return tok(p, 0); }
static void adv(Parser *p) { p->cur++; }

static int same_text(Parser *p, size_t i, const char *s) {
  Token t = tok(p, i);
  size_t n = strlen(s);
  return t.kind == T_ID && t.len == n && memcmp(t.start, s, n) == 0;
}

/* Are tokens i and i+1 adjacent in the source stream? Used to compose
 * `infix_operator <op>` and the descending range pair `>` `..<` / `>` `..=`. */
static int adjacent(Parser *p, size_t i) {
  return tok(p, i).start + tok(p, i).len == tok(p, i + 1).start;
}

/* ---------------- errors ---------------- */

static void err(Parser *p, const char *msg) {
  if (p->quiet || p->failed) return;
  p->failed = 1;
  Token t = curtok(p);
  snprintf(p->msg, sizeof p->msg, "%s (line %d, col %d)", msg, t.line, t.col);
}

static void skip_sep(Parser *p) {
  while (at(p, T_SEP)) adv(p);
}

static void save_cur(Parser *p, size_t *save) { *save = p->cur; }
static void restore_cur(Parser *p, size_t save) { p->cur = save; }

/* ---------------- names ---------------- */

static Node *piname(Parser *p) {
  if (!at_id(p)) {
    err(p, "expected a name");
    return NULL;
  }
  Node *n = pnode(p, N_NAME, curtok(p));
  adv(p);
  return n;
}

/* ---------------- lexical helpers for dispatch ---------------- */

static int type_start_kind(TokKind k) {
  switch (k) {
  case T_KW_CONST:
  case T_KW_VOID: case T_KW_BYTE: case T_KW_CHAR: case T_KW_INT:
  case T_KW_UINT: case T_KW_FLOAT: case T_KW_BOOL: case T_KW_STRING:
  case T_KW_BYTES: case T_KW_ERROR: case T_KW_FUNC:
  case T_OP_STAR: case T_OP_AMP: case T_LBRACKET:
  case T_ID:
    return 1;
  default:
    return 0;
  }
}

static int at_type_start(Parser *p) { return type_start_kind(tk(p, 0)); }

static int is_compound_assign(TokKind k) {
  switch (k) {
  case T_OP_ADD_ASSIGN: case T_OP_SUB_ASSIGN: case T_OP_MUL_ASSIGN:
  case T_OP_DIV_ASSIGN: case T_OP_MOD_ASSIGN: case T_OP_SHL_ASSIGN:
  case T_OP_SHR_ASSIGN: case T_OP_USHR_ASSIGN: case T_OP_AND_ASSIGN:
  case T_OP_OR_ASSIGN: case T_OP_XOR_ASSIGN:
    return 1;
  default:
    return 0;
  }
}

/* The lexical operator words of `infix_operator <op>` (BinaryOperatorWord). */
static int operator_kind(TokKind k) {
  switch (k) {
  case T_OP_PLUS: case T_OP_MINUS: case T_OP_STAR: case T_OP_SLASH:
  case T_OP_PERCENT:
  case T_OP_SHL: case T_OP_SHR: case T_OP_USHR: case T_OP_SHL_CY:
  case T_OP_SHR_CY:
  case T_OP_AMP: case T_OP_PIPE: case T_OP_CARET:
  case T_OP_AND: case T_OP_OR: case T_OP_QQ:
  case T_OP_LT: case T_OP_GT: case T_OP_LE: case T_OP_GE:
  case T_OP_EQ: case T_OP_NE: case T_OP_SWAP:
  case T_OP_RANGE_LE: case T_OP_RANGE_LT:
    return 1;
  default:
    return 0;
  }
}

/* ---------------- forward declarations ---------------- */

static Node *parse_expr(Parser *p);
static Node *parse_type(Parser *p);
static Node *parse_statement(Parser *p);
static Node *parse_block(Parser *p);
static Node *parse_typeargs(Parser *p);
static Node *parse_parameter(Parser *p);
static Node *parse_parameter_list(Parser *p);
static Node *parse_directive(Parser *p);
static Node *parse_arg_list(Parser *p);      /* ArgumentList, `(` already seen */
static Node *parse_initializer(Parser *p);
static Node *parse_postfix(Parser *p);
static Node *parse_trailing_block(Parser *p);
static Node *parse_import_item(Parser *p);
static Node *parse_enum_body(Parser *p);
static Node *parse_match(Parser *p);
static Node *parse_select_head(Parser *p);

/* ---------------- types (§5) ---------------- */

static Node *parse_plain_type(Parser *p);

static Node *parse_type(Parser *p) {
  if (at(p, T_KW_CONST)) {
    Token t = curtok(p);
    adv(p);
    Node *base = parse_plain_type(p);
    if (!base) return NULL;
    return punary(p, N_TCONST, t, base);
  }
  Node *base = parse_plain_type(p);
  if (!base) return NULL;
  if (at(p, T_OP_QMARK) || at(p, T_BANG)) {
    Token t = curtok(p);
    adv(p);
    return punary(p, N_TSHAPE, t, base);
  }
  return base;
}

static int metatype_id(Parser *p, size_t i) {
  return same_text(p, i, "type") || same_text(p, i, "field") ||
         same_text(p, i, "pointer") || same_text(p, i, "value") ||
         same_text(p, i, "any");
}

static Node *parse_plain_type(Parser *p) {
  switch (tk(p, 0)) {
  case T_KW_VOID: case T_KW_BYTE: case T_KW_CHAR: case T_KW_INT:
  case T_KW_UINT: case T_KW_FLOAT: case T_KW_BOOL: case T_KW_STRING:
  case T_KW_BYTES: case T_KW_ERROR: {
    Token t = curtok(p);
    adv(p);
    return pnode(p, N_TFUND, t);
  }
  case T_KW_STRUCT: case T_KW_ENUM: case T_KW_INTERFACE:
    err(p, "`struct`/`enum`/`interface` are not types (§2.2 reserve names)");
    return NULL;
  case T_KW_FUNC: {
    Token t = curtok(p);
    if (tk(p, 1) != T_LPAREN) {
      /* bare MetaType `func` */
      adv(p);
      return pnode(p, N_TFUND, t);
    }
    /* FunctionType = "func" "(" [ Type { Sep Type } ] ")" [ Type ] */
    adv(p);            /* func */
    adv(p);            /* ( */
    Node *params = pnlist(p);
    if (!at(p, T_RPAREN)) {
      for (;;) {
        Node *pt = parse_type(p);
        if (!pt) return NULL;
        ppush(p, params, pt);
        if (at(p, T_SEP)) { adv(p); continue; }
        break;
      }
    }
    if (!at(p, T_RPAREN)) { err(p, "expected ')' to close the function type"); return NULL; }
    adv(p);
    Node *ret = NULL;
    if (at_type_start(p)) {
      ret = parse_type(p);
      if (!ret) return NULL;
    }
    Node *n = pnode(p, N_TFUNC, t);
    ppush(p, n, params);
    ppush(p, n, ret);
    return n;
  }
  case T_OP_STAR: case T_OP_AMP: {
    Token t = curtok(p);
    adv(p);
    Node *base = parse_type(p);
    if (!base) return NULL;
    return punary(p, N_TVIEW, t, base);
  }
  case T_LBRACKET: {
    /* ArrayType = "[" [ Expression ] "]" Type */
    Token t = curtok(p);
    adv(p);
    Node *size = NULL;
    if (!at(p, T_RBRACKET)) {
      size = parse_expr(p);
      if (!size) return NULL;
    }
    if (!at(p, T_RBRACKET)) { err(p, "expected ']' after the array size"); return NULL; }
    adv(p);
    Node *elem = parse_type(p);
    if (!elem) return NULL;
    Node *n = pnode(p, N_TARRAY, t);
    ppush(p, n, size);
    ppush(p, n, elem);
    return n;
  }
  case T_ID: {
    Token t = curtok(p);
    if (metatype_id(p, 0)) {   /* MetaType stays a T_ID */
      adv(p);
      return pnode(p, N_TFUND, t);
    }
    Node *name = piname(p);
    if (!name) return NULL;
    Node *ta = NULL;
    if (at(p, T_LBRACKET)) {
      ta = parse_typeargs(p);
      if (!ta) return NULL;
    }
    Node *n = pnode(p, N_TNAMED, t);
    ppush(p, n, name);
    ppush(p, n, ta);
    return n;
  }
  default:
    err(p, "expected a type");
    return NULL;
  }
}

static Node *parse_typeargs(Parser *p) {
  /* caller is on `[` */
  Token t = curtok(p);
  adv(p);
  Node *n = pnode(p, N_TYPEARGS, t);
  if (at(p, T_RBRACKET)) { adv(p); return n; }
  for (;;) {
    skip_sep(p);
    if (at(p, T_RBRACKET)) { adv(p); return n; }
    if (at(p, T_EOF)) { err(p, "unterminated type arguments"); return NULL; }
    Node *arg;
    if (same_text(p, 0, "_")) {           /* TypeArg = "_" wildcard */
      Token u = curtok(p);
      adv(p);
      arg = pnode(p, N_TYPEARG, u);
    } else {
      Node *ty = parse_type(p);
      if (!ty) return NULL;
      arg = pnode(p, N_TYPEARG, ty->tok);
      ppush(p, arg, ty);
    }
    ppush(p, n, arg);
    if (at(p, T_SEP)) { adv(p); continue; }
    if (!at(p, T_RBRACKET)) { err(p, "expected ',' or ']' in type arguments"); return NULL; }
    adv(p);
    return n;
  }
}

/* ---------------- parameters (§4.7) ---------------- */

static Node *parse_parameter(Parser *p) {
  /* Parameter = [Name] [const] Type [= Expression] | Name ":=" Expression */
  Token t = curtok(p);
  Node *name = NULL, *type = NULL, *dflt = NULL;
  if (at_id(p)) {
    if (tk(p, 1) == T_OP_DEFINE) {
      Node *nm = piname(p);
      adv(p);                               /* := */
      dflt = parse_expr(p);
      if (!dflt) return NULL;
      Node *pa = pnode(p, N_PARAM, t);
      ppush(p, pa, nm);
      ppush(p, pa, NULL);
      ppush(p, pa, dflt);
      return pa;
    }
    name = piname(p);
    if (!name) return NULL;
  }
  if (at(p, T_KW_CONST)) adv(p);
  type = parse_type(p);
  if (!type) return NULL;
  if (at(p, T_OP_ASSIGN)) {
    adv(p);
    dflt = parse_expr(p);
    if (!dflt) return NULL;
  }
  Node *pa = pnode(p, N_PARAM, t);
  ppush(p, pa, name);
  ppush(p, pa, type);
  ppush(p, pa, dflt);
  return pa;
}

static Node *parse_parameter_list(Parser *p) {
  Node *n = pnlist(p);
  if (!at(p, T_LPAREN)) { err(p, "expected '('"); return NULL; }
  adv(p);
  skip_sep(p);
  if (at(p, T_RPAREN)) { adv(p); return n; }
  for (;;) {
    if (at(p, T_RPAREN)) { adv(p); return n; }
    if (at(p, T_EOF)) { err(p, "unterminated parameter list"); return NULL; }
    Node *pa = parse_parameter(p);
    if (!pa) return NULL;
    ppush(p, n, pa);
    if (at(p, T_SEP)) { adv(p); continue; }
    if (at(p, T_RPAREN)) { adv(p); return n; }
    err(p, "expected ',' or ')' in parameter list");
    return NULL;
  }
}

/* ---------------- type parameters (§4.8) ---------------- */

static Node *parse_typeparams(Parser *p) {
  if (!at(p, T_LBRACKET)) { err(p, "expected '['"); return NULL; }
  adv(p);
  Node *n = pnlist(p);
  if (at(p, T_RBRACKET)) { adv(p); return n; }
  for (;;) {
    if (at(p, T_RBRACKET)) { adv(p); return n; }
    if (at(p, T_SEP)) { adv(p); continue; }
    if (at(p, T_EOF)) { err(p, "unterminated type-parameter list"); return NULL; }
    Token t = curtok(p);
    Node *name;
    if (same_text(p, 0, "_")) {
      name = pnode(p, N_NAME, curtok(p));
      adv(p);
    } else {
      name = piname(p);
      if (!name) return NULL;
    }
    /* ch[0] = name, ch[1] = constraint name?, ch[2] = constraint args? */
    Node *con = NULL, *conargs = NULL;
    if (at(p, T_KW_STRUCT)) {
      con = pnode(p, N_NAME, curtok(p));
      adv(p);
    } else if (at(p, T_ID)) {
      con = piname(p);
      if (!con) return NULL;
      if (at(p, T_LBRACKET)) {
        conargs = parse_typeargs(p);
        if (!conargs) return NULL;
      }
    } else if (at(p, T_LBRACKET)) {
      conargs = parse_typeargs(p);
      if (!conargs) return NULL;
    }
    Node *tp = pnode(p, N_TYPEPARAM, t);
    ppush(p, tp, name);
    ppush(p, tp, con);
    ppush(p, tp, conargs);
    ppush(p, n, tp);
    if (at(p, T_SEP)) { adv(p); continue; }
    if (at(p, T_RBRACKET)) { adv(p); return n; }
    err(p, "expected ',' or ']' in type-parameter list");
    return NULL;
  }
}

/* ---------------- directives and imports (§3) ---------------- */

static Node *parse_directive(Parser *p) {
  Token t = curtok(p);
  if (!at(p, T_HASH)) { err(p, "expected '#'"); return NULL; }
  adv(p);
  Node *name = piname(p);
  if (!name) return NULL;
  Node *member = NULL, *args = NULL;
  if (at(p, T_DOT)) {
    adv(p);
    member = piname(p);
    if (!member) return NULL;
  }
  if (at(p, T_LPAREN)) {
    args = parse_arg_list(p);
    if (!args) return NULL;
  }
  Node *n = pnode(p, N_DIRECTIVE, t);
  ppush(p, n, name);
  ppush(p, n, member);
  ppush(p, n, args);
  return n;
}

static Node *parse_import_block(Parser *p) {
  /* ImportBlock = "#import" "{" { ImportItem } "}" */
  Token t = curtok(p);
  if (!at(p, T_HASH)) { err(p, "expected '#'"); return NULL; }
  adv(p);
  if (!at(p, T_KW_IMPORT)) { err(p, "expected '#import'"); return NULL; }
  adv(p);
  if (!at(p, T_LBRACE)) { err(p, "expected '{' after #import"); return NULL; }
  adv(p);
  Node *items = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated import block"); return NULL; }
    Node *item = parse_import_item(p);
    if (!item) return NULL;
    ppush(p, items, item);
    skip_sep(p);
    if (at(p, T_RBRACE) || at(p, T_EOF)) break;
    if (at(p, T_SEP)) { adv(p); continue; }
  }
  adv(p);                                 /* } */
  Node *n = pnode(p, N_IMPORT, t);
  ppush(p, n, items);
  return n;
}

static Node *parse_import_item(Parser *p) {
  /* ImportItem = ImportKind "(" StringLiteral { Sep StringLiteral } ")" */
  Token t = curtok(p);
  if (!at_id(p)) { err(p, "expected an import kind"); return NULL; }
  Node *kind = piname(p);
  if (!at(p, T_LPAREN)) { err(p, "expected '(' after the import kind"); return NULL; }
  adv(p);
  Node *strs = pnlist(p);
  skip_sep(p);
  if (!at(p, T_RPAREN)) {
    for (;;) {
      if (!at(p, T_STR)) { err(p, "expected a string literal in the import"); return NULL; }
      Node *s = pnode(p, N_LIT, curtok(p));
      adv(p);
      ppush(p, strs, s);
      if (at(p, T_SEP)) { adv(p); continue; }
      break;
    }
  }
  if (!at(p, T_RPAREN)) { err(p, "expected ')' to close the import"); return NULL; }
  adv(p);
  Node *n = pnode(p, N_IMPORT, t);
  ppush(p, n, kind);
  ppush(p, n, strs);
  return n;
}

/* ---------------- fields, struct / enum / error / interface bodies (§4) ---------------- */

static Node *parse_field_tail(Parser *p, Node *name) {
  /* FieldDecl = Name [const] Type [= Expression] [Directive*] */
  Token t = name->tok;
  if (at(p, T_KW_CONST)) adv(p);
  Node *type = parse_type(p);
  if (!type) return NULL;
  Node *dflt = NULL, *dirs = NULL;
  if (at(p, T_OP_ASSIGN)) {
    adv(p);
    dflt = parse_expr(p);
    if (!dflt) return NULL;
  }
  if (at(p, T_HASH)) {
    dirs = pnlist(p);
    while (at(p, T_HASH)) {
      Node *d = parse_directive(p);
      if (!d) return NULL;
      ppush(p, dirs, d);
    }
  }
  Node *n = pnode(p, N_FIELD, t);
  ppush(p, n, name);
  ppush(p, n, type);
  ppush(p, n, dflt);
  ppush(p, n, dirs);
  return n;
}

static Node *parse_field_decl(Parser *p) {
  Node *name = piname(p);
  if (!name) return NULL;
  return parse_field_tail(p, name);
}

static Node *parse_struct_member(Parser *p) {
  /* StructMember = FieldDecl | ConformanceMarker */
  if (!at_id(p)) { err(p, "expected a member name"); return NULL; }
  Token t = curtok(p);
  Node *name = piname(p);
  if (at(p, T_KW_INTERFACE)) {
    adv(p);
    Node *n = pnode(p, N_CONFORM, t);
    ppush(p, n, name);
    return n;
  }
  if (at(p, T_LBRACKET)) {
    /* Name [TypeArgs] "interface" vs a field with an array type */
    size_t save;
    save_cur(p, &save);
    p->quiet++;
    Node *ta = parse_typeargs(p);
    p->quiet--;
    if (ta && at(p, T_KW_INTERFACE)) {
      adv(p);
      Node *n = pnode(p, N_CONFORM, t);
      ppush(p, n, name);
      ppush(p, n, ta);
      return n;
    }
    restore_cur(p, save);
  }
  return parse_field_tail(p, name);
}

static Node *parse_struct_body(Parser *p) {
  /* StructBody = "{" { StructMember } "}" */
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated struct body"); return NULL; }
    Node *m = parse_struct_member(p);
    if (!m) return NULL;
    ppush(p, n, m);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between struct members");
    return NULL;
  }
  adv(p);                                   /* } */
  return n;
}

static Node *parse_enum_member(Parser *p) {
  /* EnumMember = Name [Type] [= Expression] | Name "(" EnumParams ")"
   *             | Name "(" "enum" "=" EnumBody ")" */
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  Node *payload = NULL, *dflt = NULL;
  if (at(p, T_LPAREN)) {
    adv(p);
    if (at(p, T_KW_ENUM)) {
      adv(p);
      if (!at(p, T_OP_ASSIGN)) { err(p, "expected '='"); return NULL; }
      adv(p);
      payload = parse_enum_body(p);
      if (!payload) return NULL;
    } else {
      payload = parse_parameter_list(p);
      if (!payload) return NULL;
    }
    if (!at(p, T_RPAREN)) { err(p, "expected ')'"); return NULL; }
    adv(p);
  } else if (at_type_start(p)) {
    payload = parse_type(p);
    if (!payload) return NULL;
    if (at(p, T_OP_ASSIGN)) {
      adv(p);
      dflt = parse_expr(p);
      if (!dflt) return NULL;
    }
  } else if (at(p, T_OP_ASSIGN)) {
    adv(p);
    dflt = parse_expr(p);
    if (!dflt) return NULL;
  }
  Node *n = pnode(p, N_ENUM_MEMBER, t);
  ppush(p, n, name);
  ppush(p, n, payload);
  ppush(p, n, dflt);
  return n;
}

static Node *parse_enum_body(Parser *p) {
  /* EnumBody = "{" { EnumMember } "}" */
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated enum body"); return NULL; }
    Node *m = parse_enum_member(p);
    if (!m) return NULL;
    ppush(p, n, m);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between enum members");
    return NULL;
  }
  adv(p);                                   /* } */
  return n;
}

static Node *parse_method_sig(Parser *p) {
  /* MethodSig = Name "func" [TypeParams] "(" [ParameterList] ")" [Type] [Directive] */
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  if (!at(p, T_KW_FUNC)) { err(p, "expected 'func'"); return NULL; }
  adv(p);
  Node *tp = NULL;
  if (at(p, T_LBRACKET)) {
    tp = parse_typeparams(p);
    if (!tp) return NULL;
  }
  Node *params = parse_parameter_list(p);
  if (!params) return NULL;
  Node *ret = NULL;
  if (at_type_start(p)) {
    ret = parse_type(p);
    if (!ret) return NULL;
  }
  Node *dir = NULL;
  if (at(p, T_HASH)) {
    dir = parse_directive(p);
    if (!dir) return NULL;
  }
  Node *n = pnode(p, N_METHOD, t);
  ppush(p, n, name);
  ppush(p, n, tp);
  ppush(p, n, params);
  ppush(p, n, ret);
  ppush(p, n, dir);
  return n;
}

/* ---------------- expressions (§7) ---------------- */

static Node *parse_unary(Parser *p);
static Node *parse_primary(Parser *p);

static Node *parse_ladder(Parser *p, Node *(*next)(Parser *),
                          const TokKind *ops, size_t nops) {
  Node *l = next(p);
  if (!l) return NULL;
  for (;;) {
    TokKind k = tk(p, 0);
    size_t i;
    for (i = 0; i < nops; i++) if (ops[i] == k) break;
    if (i == nops) return l;
    Token t = curtok(p);
    adv(p);
    Node *r = next(p);
    if (!r) return NULL;
    Node *n = pnode(p, N_BIN, t);
    ppush(p, n, l);
    ppush(p, n, r);
    l = n;
  }
}

#define LADDER(name, nextfn, ...)                                   \
  static const TokKind name##_ops[] = { __VA_ARGS__ };              \
  static Node *name(Parser *p) {                                    \
    return parse_ladder(p, nextfn, name##_ops,                      \
                        sizeof name##_ops / sizeof name##_ops[0]);  \
  }

LADDER(parse_mul, parse_unary,
       T_OP_STAR, T_OP_SLASH, T_OP_PERCENT)
LADDER(parse_add, parse_mul,
       T_OP_PLUS, T_OP_MINUS)
LADDER(parse_shift, parse_add,
       T_OP_SHL, T_OP_SHR, T_OP_USHR, T_OP_SHL_CY, T_OP_SHR_CY)

/* RangeExpr = ShiftExpr { RangeOp ShiftExpr } — the `>` `..<` / `>` `..=`
 * token pair binds here, as one left-associative operator (§7, C23). */
static Node *parse_range(Parser *p) {
  Node *l = parse_shift(p);
  if (!l) return NULL;
  for (;;) {
    TokKind k = tk(p, 0);
    if (k == T_OP_RANGE_LE || k == T_OP_RANGE_LT) {
      Token t = curtok(p);
      adv(p);
      Node *r = parse_shift(p);
      if (!r) return NULL;
      Node *n = pnode(p, N_BIN, t);
      ppush(p, n, l);
      ppush(p, n, r);
      l = n;
    } else if (k == T_OP_GT &&
               (tk(p, 1) == T_OP_RANGE_LE || tk(p, 1) == T_OP_RANGE_LT) &&
               adjacent(p, 0)) {
      /* descending range: the adjacent pair `>` `..=` / `>` `..<` */
      Token t = tok(p, 1);
      adv(p);                                   /* > */
      adv(p);                                   /* ..= / ..< */
      Node *r = parse_shift(p);
      if (!r) return NULL;
      Node *n = pnode(p, N_BIN, t);
      ppush(p, n, l);
      ppush(p, n, r);
      l = n;
    } else {
      break;
    }
  }
  return l;
}

LADDER(parse_rel, parse_range,
       T_OP_LT, T_OP_GT, T_OP_LE, T_OP_GE)
LADDER(parse_eq, parse_rel,
       T_OP_EQ, T_OP_NE)
LADDER(parse_bitand_expr, parse_eq, T_OP_AMP)
LADDER(parse_bitxor, parse_bitand_expr, T_OP_CARET)
LADDER(parse_bitor, parse_bitxor, T_OP_PIPE)
LADDER(parse_and, parse_bitor, T_OP_AND)
LADDER(parse_or, parse_and, T_OP_OR)

static Node *parse_fallback(Parser *p) {
  Node *l = parse_or(p);
  if (!l) return NULL;
  if (at(p, T_OP_QQ)) {
    Token t = curtok(p);
    adv(p);
    Node *r = parse_or(p);
    if (!r) return NULL;
    Node *n = pnode(p, N_BIN, t);
    ppush(p, n, l);
    ppush(p, n, r);
    l = n;
  }
  return l;
}

static Node *parse_expr(Parser *p) {
  /* Expression = FallbackExpr | IsExpr | InitializerLiteral */
  if (at(p, T_LBRACE)) return parse_initializer(p);
  Node *e = parse_fallback(p);
  if (!e) return NULL;
  if (at(p, T_KW_IS)) {
    /* IsExpr = EqualityExpr "is" Name [ Name ] */
    Token t = curtok(p);
    adv(p);
    Node *n1 = piname(p);
    if (!n1) return NULL;
    Node *n2 = NULL;
    if (at_id(p)) {
      n2 = piname(p);
      if (!n2) return NULL;
    }
    Node *n = pnode(p, N_IS, t);
    ppush(p, n, e);
    ppush(p, n, n1);
    ppush(p, n, n2);
    return n;
  }
  return e;
}

/* Head expression of a checked `if`/`loop` (and the match subject, loop
 * clauses): suppress postfix `?` so the trailing `?` of the checked form is
 * left for the caller, and suppress `Name "{"` struct literals so a block
 * that follows the head (`if b { … }`, `match status { … }`) stays a block. */
static Node *parse_no_q_expr(Parser *p) {
  int s1 = p->no_q, s2 = p->in_head;
  p->no_q = 1;
  p->in_head = 1;
  Node *e = parse_expr(p);
  p->no_q = s1;
  p->in_head = s2;
  return e;
}

/* Head mode without the `?` suppression (the match subject, loop collection /
 * condition): `Name "{"` must stay a block after the head. */
static Node *parse_head_plain(Parser *p) {
  int saved = p->in_head;
  p->in_head = 1;
  Node *e = parse_expr(p);
  p->in_head = saved;
  return e;
}

static Node *parse_unary(Parser *p) {
  TokKind k = tk(p, 0);
  if (k == T_OP_MINUS || k == T_BANG || k == T_OP_TILDE || k == T_OP_AMP) {
    Token t = curtok(p);
    adv(p);
    Node *o = parse_unary(p);
    if (!o) return NULL;
    return punary(p, N_UNARY, t, o);
  }
  if (k == T_OP_RECV) {                 /* ReceiveExpr = "<-" UnaryExpr */
    Token t = curtok(p);
    adv(p);
    Node *o = parse_unary(p);
    if (!o) return NULL;
    return punary(p, N_RECV, t, o);
  }
  return parse_postfix(p);
}

static Node *parse_args_and_call(Parser *p, Node *callee, Node *typeargs) {
  Node *args = parse_arg_list(p);
  if (!args) return NULL;
  Node *n = pnode(p, N_CALL, callee->tok);
  ppush(p, n, callee);
  ppush(p, n, typeargs);
  ppush(p, n, args);
  return n;
}

static Node *parse_accessor(Parser *p, Node *base) {
  /* on `[`; consume through `]` — a[i], s[:n], [:], a[lo..=hi] */
  Token t = curtok(p);
  adv(p);
  if (at(p, T_COLON)) {
    adv(p);
    Node *hi = NULL;
    if (!at(p, T_RBRACKET)) {
      hi = parse_expr(p);
      if (!hi) return NULL;
    }
    if (!at(p, T_RBRACKET)) { err(p, "expected ']'"); return NULL; }
    adv(p);
    Node *n = pnode(p, N_SLICE, t);
    ppush(p, n, base);
    ppush(p, n, NULL);
    ppush(p, n, hi);
    return n;
  }
  Node *lo = parse_expr(p);
  if (!lo) return NULL;
  if (at(p, T_COLON)) {
    adv(p);
    Node *hi = NULL;
    if (!at(p, T_RBRACKET)) {
      hi = parse_expr(p);
      if (!hi) return NULL;
    }
    if (!at(p, T_RBRACKET)) { err(p, "expected ']'"); return NULL; }
    adv(p);
    Node *n = pnode(p, N_SLICE, t);
    ppush(p, n, base);
    ppush(p, n, lo);
    ppush(p, n, hi);
    return n;
  }
  if (!at(p, T_RBRACKET)) { err(p, "expected ']' or ':'"); return NULL; }
  adv(p);
  Node *n = pnode(p, N_INDEX, t);
  ppush(p, n, base);
  ppush(p, n, lo);
  return n;
}

static Node *parse_postfix(Parser *p) {
  Node *e = parse_primary(p);
  if (!e) return NULL;
  for (;;) {
    switch (tk(p, 0)) {
    case T_LPAREN:
      e = parse_args_and_call(p, e, NULL);
      if (!e) return NULL;
      break;
    case T_LBRACKET: {
      /* Typed call `e[T](…)` / typed struct literal `e[K,V] { … }` vs
       * an index/slice accessor.  Only a type-args list immediately
       * followed by `(` or `{` is a typed form. */
      size_t save;
      save_cur(p, &save);
      p->quiet++;
      Node *ta = parse_typeargs(p);
      p->quiet--;
      if (ta && (at(p, T_LPAREN) || at(p, T_LBRACE))) {
        if (at(p, T_LPAREN)) {
          e = parse_args_and_call(p, e, ta);
          if (!e) return NULL;
        } else {
          Node *init = parse_initializer(p);
          if (!init) return NULL;
          Node *n = pnode(p, N_STRUCT_LIT, e->tok);
          ppush(p, n, e);       /* the type name */
          ppush(p, n, ta);
          ppush(p, n, init);
          e = n;
        }
        break;
      }
      restore_cur(p, save);
      e = parse_accessor(p, e);
      if (!e) return NULL;
      break;
    }
    case T_DOT: {
      adv(p);
      Node *name = piname(p);
      if (!name) return NULL;
      Node *mem = pnode(p, N_MEMBER, name->tok);
      ppush(p, mem, e);
      ppush(p, mem, name);
      e = mem;
      /* `e.Name [TypeArgs] "(" … ")"` — method with explicit type args */
      if (at(p, T_LBRACKET)) {
        size_t save;
        save_cur(p, &save);
        p->quiet++;
        Node *ta = parse_typeargs(p);
        p->quiet--;
        if (ta && at(p, T_LPAREN)) {
          e = parse_args_and_call(p, e, ta);
          if (!e) return NULL;
          break;
        }
        restore_cur(p, save);
      }
      break;
    }
    case T_BANG:
      { Token t = curtok(p);
        adv(p);
        e = punary(p, N_UNWRAP, t, e); }
      break;
    case T_OP_QMARK:
      if (p->no_q) return e;
      { Token t = curtok(p);
        adv(p);
        e = punary(p, N_UNWRAP, t, e); }
      break;
    default:
      return e;
    }
  }
}

static Node *parse_initializer(Parser *p) {
  /* InitializerLiteral = "{}" | "{" Element { Sep Element } "}" */
  Token t = curtok(p);
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnode(p, N_INIT, t);
  for (;;) {
    skip_sep(p);                          /* elements may start on the next line */
    if (at(p, T_RBRACE)) { adv(p); return n; }
    if (at(p, T_EOF)) { err(p, "unterminated initializer"); return NULL; }
    Node *item;
    if (at(p, T_LBRACE)) {
      Node *inner = parse_initializer(p);
      if (!inner) return NULL;
      item = pnode(p, N_INITITEM, inner->tok);
      ppush(p, item, inner);
    } else if (at_id(p) && tk(p, 1) == T_OP_ASSIGN) {
      Node *name = piname(p);
      adv(p);                               /* = */
      Node *v = parse_expr(p);
      if (!v) return NULL;
      item = pnode(p, N_INITITEM, name->tok);
      ppush(p, item, name);
      ppush(p, item, v);
    } else {
      Node *v = parse_expr(p);
      if (!v) return NULL;
      item = pnode(p, N_INITITEM, v->tok);
      ppush(p, item, v);
    }
    ppush(p, n, item);
    if (at(p, T_SEP)) { adv(p); continue; }
    if (at(p, T_RBRACE)) { adv(p); return n; }
    err(p, "expected ',' or '}' in initializer");
    return NULL;
  }
}

static Node *parse_arg_list(Parser *p) {
  /* ArgumentList = CallArg { Sep CallArg } — caller is on `(` */
  Token t = curtok(p);
  if (!at(p, T_LPAREN)) { err(p, "expected '('"); return NULL; }
  adv(p);
  skip_sep(p);
  Node *n = pnode(p, N_LIST, t);
  if (at(p, T_RPAREN)) { adv(p); return n; }
  for (;;) {
    if (at(p, T_RPAREN)) { adv(p); return n; }
    if (at(p, T_EOF)) { err(p, "unterminated argument list"); return NULL; }
    Node *arg;
    if (at(p, T_LBRACE)) {
      /* TrailingBlock, or a `{…}` initializer expression */
      size_t save;
      save_cur(p, &save);
      p->quiet++;
      Node *tb = parse_trailing_block(p);
      p->quiet--;
      if (tb) {
        arg = tb;
      } else {
        restore_cur(p, save);
        arg = parse_initializer(p);
        if (!arg) return NULL;
      }
    } else if (at_id(p) && tk(p, 1) == T_OP_ASSIGN) {
      Node *name = piname(p);
      adv(p);                               /* = */
      Node *v = parse_expr(p);
      if (!v) return NULL;
      arg = pnode(p, N_INITITEM, name->tok);
      ppush(p, arg, name);
      ppush(p, arg, v);
    } else {
      arg = parse_expr(p);
      if (!arg) return NULL;
    }
    ppush(p, n, arg);
    if (at(p, T_SEP)) { adv(p); continue; }
    if (at(p, T_RPAREN)) { adv(p); return n; }
    err(p, "expected ',' or ')' in argument list");
    return NULL;
  }
}

static Node *parse_trailing_block(Parser *p) {
  /* TrailingBlock = "{" Expression "}" | "{" Name { Sep Name } ":" Expression "}" */
  Token t = curtok(p);
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  size_t save;
  save_cur(p, &save);
  p->quiet++;
  Node *e = parse_expr(p);
  int expr_ok = e != NULL && at(p, T_RBRACE);
  p->quiet--;
  restore_cur(p, save);
  if (expr_ok) {
    adv(p);                               /* { */
    Node *body = parse_expr(p);
    if (!body) return NULL;
    if (!at(p, T_RBRACE)) { err(p, "expected '}'"); return NULL; }
    adv(p);
    Node *n = pnode(p, N_TRAILING, t);
    ppush(p, n, body);
    return n;
  }
  adv(p);                                 /* { */
  Node *names = pnlist(p);
  skip_sep(p);
  for (;;) {
    if (at(p, T_EOF) || at(p, T_RBRACE)) { err(p, "expected parameter names, then ':'"); return NULL; }
    if (!at_id(p)) { err(p, "expected a parameter name"); return NULL; }
    Node *nm = piname(p);
    ppush(p, names, nm);
    skip_sep(p);
    if (at(p, T_COLON)) { adv(p); break; }
    if (at(p, T_SEP)) { adv(p); skip_sep(p); continue; }
    err(p, "expected ':' after the trailing-block parameters");
    return NULL;
  }
  Node *body = parse_expr(p);
  if (!body) return NULL;
  if (!at(p, T_RBRACE)) { err(p, "expected '}'"); return NULL; }
  adv(p);
  Node *n = pnode(p, N_TRAILING, t);
  ppush(p, n, names);
  ppush(p, n, body);
  return n;
}

static Node *parse_func_expr(Parser *p) {
  /* FunctionExpr = "func" "(" [LambdaParamList] ")" [Type] Block */
  Token t = curtok(p);
  if (!at(p, T_KW_FUNC)) { err(p, "expected 'func'"); return NULL; }
  adv(p);
  if (!at(p, T_LPAREN)) { err(p, "expected '('"); return NULL; }
  adv(p);
  Node *params = pnlist(p);
  if (!at(p, T_RPAREN)) {
    for (;;) {
      Node *pa;
      if (at_id(p) && (tk(p, 1) == T_SEP || tk(p, 1) == T_RPAREN)) {
        /* names-only LambdaParam: `func (a, b) { … }` */
        Node *nm = piname(p);
        pa = pnode(p, N_PARAM, nm->tok);
        ppush(p, pa, nm);
        ppush(p, pa, NULL);
        ppush(p, pa, NULL);
      } else {
        pa = parse_parameter(p);
        if (!pa) return NULL;
      }
      ppush(p, params, pa);
      if (at(p, T_SEP)) { adv(p); continue; }
      break;
    }
  }
  if (!at(p, T_RPAREN)) { err(p, "expected ')'"); return NULL; }
  adv(p);
  Node *ret = NULL;
  if (at_type_start(p)) {
    ret = parse_type(p);
    if (!ret) return NULL;
  }
  Node *body = parse_block(p);
  if (!body) return NULL;
  Node *n = pnode(p, N_FUNC_EXPR, t);
  ppush(p, n, params);
  ppush(p, n, ret);
  ppush(p, n, body);
  return n;
}

static Node *parse_anon_struct(Parser *p) {
  /* AnonymousStructExpr = "struct" [ "{" { FieldDecl } "}" ] */
  Token t = curtok(p);
  if (!at(p, T_KW_STRUCT)) { err(p, "expected 'struct'"); return NULL; }
  adv(p);
  Node *fields = NULL;
  if (at(p, T_LBRACE)) {
    adv(p);
    fields = pnlist(p);
    skip_sep(p);
    while (!at(p, T_RBRACE)) {
      if (at(p, T_EOF)) { err(p, "unterminated anonymous struct"); return NULL; }
      Node *f = parse_field_decl(p);
      if (!f) return NULL;
      ppush(p, fields, f);
      if (at(p, T_SEP)) { skip_sep(p); continue; }
      if (at(p, T_RBRACE)) break;
      err(p, "expected a separator between fields");
      return NULL;
    }
    adv(p);                               /* } */
  }
  Node *n = pnode(p, N_ANON_STRUCT, t);
  ppush(p, n, fields);
  return n;
}

static Node *parse_if_expr(Parser *p) {
  /* IfExpr = "if" Expression "then" Expression "else" Expression */
  Token t = curtok(p);
  if (!at(p, T_KW_IF)) { err(p, "expected 'if'"); return NULL; }
  adv(p);
  Node *cond = parse_expr(p);
  if (!cond) return NULL;
  if (!at(p, T_KW_THEN)) { err(p, "expected 'then' in the if expression"); return NULL; }
  adv(p);
  Node *a = parse_expr(p);
  if (!a) return NULL;
  if (!at(p, T_KW_ELSE)) { err(p, "expected 'else' in the if expression"); return NULL; }
  adv(p);
  Node *b = parse_expr(p);
  if (!b) return NULL;
  Node *n = pnode(p, N_IF, t);
  ppush(p, n, cond);
  ppush(p, n, a);
  ppush(p, n, b);
  return n;
}

static Node *parse_primary(Parser *p) {
  /* PrimaryExpr = Literal | Name | "(" Expression ")" | InitializerLiteral
   *             | NamedStructLiteral | AnonymousStructExpr | FunctionExpr
   *             | MatchExpr | IfExpr */
  switch (tk(p, 0)) {
  case T_NUM: case T_CHAR: case T_STR: case T_KW_TRUE: case T_KW_FALSE: {
    Token t = curtok(p);
    adv(p);
    return pnode(p, N_LIT, t);
  }
  case T_LBRACE:
    return parse_initializer(p);
  case T_KW_STRUCT:
    return parse_anon_struct(p);
  case T_KW_FUNC:
    return parse_func_expr(p);
  case T_KW_MATCH:
    return parse_match(p);
  case T_KW_IF:
    return parse_if_expr(p);
  case T_KW_BYTE: case T_KW_CHAR: case T_KW_INT: case T_KW_UINT:
  case T_KW_FLOAT: case T_KW_BOOL: case T_KW_STRING: case T_KW_BYTES:
  case T_KW_ERROR: {
    /* A fundamental type used as a first-class value — the `X.from(y)`
     * cast family is `PrimaryExpr "." "from" "(" … ")"` (§README from). */
    Token t = curtok(p);
    adv(p);
    return pnode(p, N_NAME, t);
  }
  case T_ID: {
    Token t = curtok(p);
    Node *name = piname(p);
    if (!name) return NULL;
    if (!p->in_head && at(p, T_LBRACE)) {
      /* NamedStructLiteral = Name "{" InitElements "}" */
      Node *init = parse_initializer(p);
      if (!init) return NULL;
      Node *n = pnode(p, N_STRUCT_LIT, t);
      ppush(p, n, name);
      ppush(p, n, NULL);                 /* type args */
      ppush(p, n, init);
      return n;
    }
    return name;
  }
  case T_LPAREN: {
    adv(p);
    Node *e = parse_expr(p);
    if (!e) return NULL;
    if (!at(p, T_RPAREN)) { err(p, "expected ')'"); return NULL; }
    adv(p);
    return e;                            /* parens are transparent; the `?`
                                            view-unwrap is a postfix form */
  }
  case T_OP_ARROW:
    err(p, "`->` has no syntax role (open item §9)");
    return NULL;
  case T_OP_INC:
    err(p, "`++` is not in the language — use `i += 1`");
    return NULL;
  case T_OP_DEC:
    err(p, "`--` is not in the language — use `i -= 1`");
    return NULL;
  default:
    err(p, "expected an expression");
    return NULL;
  }
}

static Node *parse_pattern(Parser *p) {
  /* Pattern = Wildcard | Literal | GuardExpr | ConstructorPattern */
  if (same_text(p, 0, "_")) {
    Token t = curtok(p);
    adv(p);
    return pnode(p, N_NAME, t);
  }
  if (at_id(p)) {
    Node *name = piname(p);
    if (!name) return NULL;
    if (at(p, T_LPAREN)) {
      /* ConstructorPattern = Name "(" PatternList ")" */
      adv(p);
      Node *inner = pnlist(p);
      if (!at(p, T_RPAREN)) {
        for (;;) {
          Node *pat = parse_pattern(p);
          if (!pat) return NULL;
          ppush(p, inner, pat);
          if (at(p, T_SEP)) { adv(p); continue; }
          break;
        }
      }
      if (!at(p, T_RPAREN)) { err(p, "expected ')'"); return NULL; }
      adv(p);
      Node *n = pnode(p, N_CALL, name->tok);   /* constructor-pattern shape */
      ppush(p, n, name);
      ppush(p, n, NULL);
      ppush(p, n, inner);
      return n;
    }
    return name;
  }
  return parse_expr(p);                        /* Literal / GuardExpr */
}

/* Try to read `Pattern { Sep Pattern } "=>"` at the cursor without lasting
 * effect.  True means the arm body must end here: a new arm head follows. */
static int looks_like_arm_head(Parser *p) {
  size_t save;
  save_cur(p, &save);
  int q = p->quiet;
  p->quiet++;
  int ok = 0;
  int line = -1;                            /* a head stays on one line */
  Node *pat;
  while ((pat = parse_pattern(p)) != NULL) {
    if (line < 0) line = pat->line;
    else if (pat->line != line) break;
    if (at(p, T_OP_FATARROW)) { ok = 1; break; }
    if (at(p, T_SEP)) { adv(p); continue; }   /* next pattern in the list */
    break;
  }
  p->quiet = q;
  restore_cur(p, save);
  return ok;
}

/* ArmBody = Block | Statement | { Statement } — the unbraced list ends where
 * an arm head starts on a following line (§6.5, §6.7).  `select_mode`
 * switches the head detector: match arms restart with a pattern list + `=>`,
 * select arms restart with a select head + `=>`. */
static Node *parse_arm_body(Parser *p, int select_mode) {
  if (at(p, T_LBRACE)) return parse_block(p);
  Node *lst = pnlist(p);
  for (;;) {
    if (at(p, T_RBRACE) || at(p, T_EOF)) break;
    /* Peek across separators: the unbraced list ends where an arm head
     * starts on a following line.  Stop with cur still on the separator so
     * the caller's arms loop can see it. */
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      int q = p->quiet;
      p->quiet++;
      int head = select_mode
        ? (parse_select_head(p) != NULL && at(p, T_OP_FATARROW))
        : looks_like_arm_head(p);
      p->quiet = q;
      restore_cur(p, save);
      if (head) break;
    }
    if (at(p, T_SEP)) { adv(p); continue; }
    Node *st = parse_statement(p);
    if (!st) return NULL;
    ppush(p, lst, st);
    if (!at(p, T_SEP) && !at(p, T_RBRACE) && !at(p, T_EOF)) {
      err(p, "expected a separator in the arm body");
      return NULL;
    }
    /* loop back: the peek finds the next arm head (leaving the separator
     * for the caller) or the HEAD separator is consumed before the next
     * statement */
  }
  return lst;
}

static Node *parse_match_arm(Parser *p) {
  /* MatchArm = PatternList "=>" ArmBody */
  Node *pats = pnlist(p);
  for (;;) {
    Node *pat = parse_pattern(p);
    if (!pat) return NULL;
    ppush(p, pats, pat);
    if (at(p, T_OP_FATARROW)) { adv(p); break; }
    if (at(p, T_SEP)) { adv(p); continue; }
    err(p, "expected '=>' after the match patterns");
    return NULL;
  }
  Node *body = parse_arm_body(p, 0);
  if (!body) return NULL;
  Node *n = pnode(p, N_MATCH_ARM, pats->tok);
  ppush(p, n, pats);
  ppush(p, n, body);
  return n;
}

static Node *parse_match(Parser *p) {
  /* MatchStmt / MatchExpr = "match" Expression "{" { MatchArm } "}" */
  Token t = curtok(p);
  if (!at(p, T_KW_MATCH)) { err(p, "expected 'match'"); return NULL; }
  adv(p);
  Node *subject = parse_head_plain(p);
  if (!subject) return NULL;
  if (!at(p, T_LBRACE)) { err(p, "expected '{' after the match subject"); return NULL; }
  adv(p);
  Node *arms = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated match"); return NULL; }
    Node *arm = parse_match_arm(p);
    if (!arm) return NULL;
    ppush(p, arms, arm);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between match arms");
    return NULL;
  }
  adv(p);                                 /* } */
  Node *n = pnode(p, N_MATCH, t);
  ppush(p, n, subject);
  ppush(p, n, arms);
  return n;
}

static Node *parse_select_head(Parser *p) {
  /* SelectHead = "default" | ReceiveExpr | "(" ReceiveExpr ")" "?"
   *             | Name ":=" ReceiveExpr | SendStmt */
  if (at(p, T_KW_DEFAULT)) {
    Token t = curtok(p);
    adv(p);
    return pnode(p, N_LIT, t);
  }
  if (at_id(p) && tk(p, 1) == T_OP_DEFINE) {
    Token t = curtok(p);
    Node *name = piname(p);
    adv(p);                               /* := */
    if (!at(p, T_OP_RECV)) { err(p, "expected '<-' in the select head"); return NULL; }
    Token rt = curtok(p);
    adv(p);
    Node *o = parse_unary(p);
    if (!o) return NULL;
    Node *recv = punary(p, N_RECV, rt, o);
    Node *n = pnode(p, N_ASSIGN, t);
    ppush(p, n, name);
    ppush(p, n, recv);
    return n;
  }
  if (at(p, T_OP_RECV)) {
    Token t = curtok(p);
    adv(p);
    Node *o = parse_unary(p);
    if (!o) return NULL;
    return punary(p, N_RECV, t, o);
  }
  if (at(p, T_LPAREN)) {
    adv(p);
    if (!at(p, T_OP_RECV)) { err(p, "expected '<-' inside the parenthesized head"); return NULL; }
    Token t = curtok(p);
    adv(p);
    Node *o = parse_unary(p);
    if (!o) return NULL;
    Node *recv = punary(p, N_RECV, t, o);
    if (!at(p, T_RPAREN)) { err(p, "expected ')'"); return NULL; }
    adv(p);
    if (!at(p, T_OP_QMARK)) { err(p, "expected '?' after the parenthesized receive"); return NULL; }
    Token qt = curtok(p);
    adv(p);
    return punary(p, N_UNWRAP, qt, recv);
  }
  Node *lhs = parse_expr(p);
  if (!lhs) return NULL;
  if (at(p, T_OP_RECV)) {                 /* SendStmt */
    Token t = curtok(p);
    adv(p);
    Node *rhs = parse_expr(p);
    if (!rhs) return NULL;
    Node *n = pnode(p, N_SEND, t);
    ppush(p, n, lhs);
    ppush(p, n, rhs);
    return n;
  }
  err(p, "expected a select head ('<-', receive, or send)");
  return NULL;
}

static Node *parse_select_arm(Parser *p) {
  Node *head = parse_select_head(p);
  if (!head) return NULL;
  if (!at(p, T_OP_FATARROW)) { err(p, "expected '=>'"); return NULL; }
  adv(p);
  Node *body = parse_arm_body(p, 1);
  if (!body) return NULL;
  Node *n = pnode(p, N_SELECT_ARM, head->tok);
  ppush(p, n, head);
  ppush(p, n, body);
  return n;
}

static Node *parse_select(Parser *p) {
  /* SelectStmt = "select" "{" { SelectArm } "}" */
  Token t = curtok(p);
  if (!at(p, T_KW_SELECT)) { err(p, "expected 'select'"); return NULL; }
  adv(p);
  if (!at(p, T_LBRACE)) { err(p, "expected '{' after select"); return NULL; }
  adv(p);
  Node *arms = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated select"); return NULL; }
    Node *arm = parse_select_arm(p);
    if (!arm) return NULL;
    ppush(p, arms, arm);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between select arms");
    return NULL;
  }
  adv(p);                                 /* } */
  Node *n = pnode(p, N_SELECT, t);
  ppush(p, n, arms);
  return n;
}

/* ---------------- statements (§6) ---------------- */

static Node *parse_var_decl(Parser *p) {
  /* VariableDecl = Name [const] Type [= Initializer] [Directive]
   *              | Name ":=" Initializer [Directive] */
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  Node *type = NULL, *init = NULL, *dir = NULL;
  if (at(p, T_OP_DEFINE)) {
    adv(p);
    init = parse_expr(p);
    if (!init) return NULL;
  } else {
    if (at(p, T_KW_CONST)) adv(p);
    type = parse_type(p);
    if (!type) return NULL;
    if (at(p, T_OP_ASSIGN)) {
      adv(p);
      init = parse_expr(p);
      if (!init) return NULL;
    }
  }
  if (at(p, T_HASH)) {
    dir = parse_directive(p);
    if (!dir) return NULL;
  }
  Node *n = pnode(p, N_VAR, t);
  ppush(p, n, name);
  ppush(p, n, type);
  ppush(p, n, init);
  ppush(p, n, dir);
  return n;
}

static Node *parse_expr_stmt(Parser *p) {
  Node *e = parse_expr(p);
  if (!e) return NULL;
  TokKind k = tk(p, 0);
  if (k == T_OP_ASSIGN || is_compound_assign(k)) {
    Token t = curtok(p);
    adv(p);
    Node *rhs = parse_expr(p);
    if (!rhs) return NULL;
    Node *n = pnode(p, N_ASSIGN, t);
    ppush(p, n, e);
    ppush(p, n, rhs);
    return n;
  }
  if (k == T_OP_SWAP) {
    Token t = curtok(p);
    adv(p);
    Node *r = parse_expr(p);
    if (!r) return NULL;
    Node *n = pnode(p, N_SWAP, t);
    ppush(p, n, e);
    ppush(p, n, r);
    return n;
  }
  if (k == T_OP_RECV) {                  /* SendStmt = Expression "<-" Expression */
    Token t = curtok(p);
    adv(p);
    Node *v = parse_expr(p);
    if (!v) return NULL;
    Node *n = pnode(p, N_SEND, t);
    ppush(p, n, e);
    ppush(p, n, v);
    return n;
  }
  return punary(p, N_EXPR_STMT, e->tok, e);
}

static Node *parse_loop_body(Parser *p) {
  /* Body = "do" Statement | Block */
  if (at(p, T_KW_DO)) {
    adv(p);
    skip_sep(p);                          /* `do` may end the line */
    return parse_statement(p);
  }
  if (at(p, T_LBRACE)) return parse_block(p);
  err(p, "expected 'do' or a block after the loop clause");
  return NULL;
}

static Node *parse_cfor_post(Parser *p) {
  /* The C-`for` step is `";" Expression` per EBNF, but witnesses use an
   * assignment there (`it = next(it)`, `i += 1`): accept either. */
  Node *e = parse_expr(p);
  if (!e) return NULL;
  TokKind k = tk(p, 0);
  if (k == T_OP_ASSIGN || is_compound_assign(k)) {
    Token t = curtok(p);
    adv(p);
    Node *rhs = parse_expr(p);
    if (!rhs) return NULL;
    Node *n = pnode(p, N_ASSIGN, t);
    ppush(p, n, e);
    ppush(p, n, rhs);
    return n;
  }
  return e;
}

static Node *parse_loop(Parser *p) {
  /* LoopStmt = [Label ":"] "loop" LoopClause */
  Token t = curtok(p);
  if (!at(p, T_KW_LOOP)) { err(p, "expected 'loop'"); return NULL; }
  adv(p);
  if (at(p, T_LBRACE)) {
    /* LoopClause = Block [ "until" Expression ] */
    Node *blk = parse_block(p);
    if (!blk) return NULL;
    Node *until = NULL;
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      if (at(p, T_KW_UNTIL)) {
        adv(p);
        skip_sep(p);
        until = parse_expr(p);
        if (!until) return NULL;
      } else {
        restore_cur(p, save);     /* leave the separator for the caller */
      }
    }
    Node *n = pnode(p, N_LOOP, t);
    ppush(p, n, blk);
    ppush(p, n, until);
    return n;
  }
  if (at_id(p) && tk(p, 1) == T_OP_DEFINE) {
    /* CForLoopClause | CheckedInClause */
    Node *name = piname(p);
    adv(p);                               /* := */
    Node *init = parse_no_q_expr(p);
    if (!init) return NULL;
    if (at(p, T_OP_QMARK)) {
      Token qt = curtok(p);
      adv(p);
      Node *body = parse_loop_body(p);
      if (!body) return NULL;
      Node *n = pnode(p, N_CHECK_IN, qt);
      ppush(p, n, name);
      ppush(p, n, init);
      ppush(p, n, body);
      return n;
    }
    if (at(p, T_SEP)) {
      adv(p);                             /* ; */
      Node *cond = parse_head_plain(p);
      if (!cond) return NULL;
      Node *post = NULL;
      if (at(p, T_SEP)) {
        adv(p);
        post = parse_cfor_post(p);
        if (!post) return NULL;
      }
      Node *body = parse_loop_body(p);
      if (!body) return NULL;
      Node *n = pnode(p, N_CFOR, t);
      ppush(p, n, name);
      ppush(p, n, init);
      ppush(p, n, cond);
      ppush(p, n, post);
      ppush(p, n, body);
      return n;
    }
    err(p, "expected '?' or ';' after the loop binding");
    return NULL;
  }
  if (at_id(p) && tk(p, 1) == T_KW_IN) {
    /* InClause = NameOrWildcard "in" Expression Body */
    Node *name = piname(p);
    adv(p);                               /* in */
    Node *coll = parse_head_plain(p);
    if (!coll) return NULL;
    Node *body = parse_loop_body(p);
    if (!body) return NULL;
    Node *n = pnode(p, N_IN_CLAUSE, t);
    ppush(p, n, NULL);
    ppush(p, n, name);
    ppush(p, n, coll);
    ppush(p, n, body);
    return n;
  }
  if (at_id(p) && tk(p, 1) == T_SEP && tk(p, 2) == T_ID && tk(p, 3) == T_KW_IN) {
    /* InClause = Ordinal "," NameOrWildcard "in" Expression Body */
    Node *ord = piname(p);
    adv(p);                               /* , */
    Node *name = piname(p);
    adv(p);                               /* in */
    Node *coll = parse_head_plain(p);
    if (!coll) return NULL;
    Node *body = parse_loop_body(p);
    if (!body) return NULL;
    Node *n = pnode(p, N_IN_CLAUSE, t);
    ppush(p, n, ord);
    ppush(p, n, name);
    ppush(p, n, coll);
    ppush(p, n, body);
    return n;
  }
  /* LoopClause = Expression "do" Statement | Expression Block */
  Node *cond = parse_head_plain(p);
  if (!cond) return NULL;
  Node *body = parse_loop_body(p);
  if (!body) return NULL;
  Node *n = pnode(p, N_LOOP, t);
  ppush(p, n, cond);
  ppush(p, n, body);
  return n;
}

static Node *parse_if(Parser *p) {
  Token t = curtok(p);
  if (!at(p, T_KW_IF)) { err(p, "expected 'if'"); return NULL; }
  adv(p);
  /* CheckedIf = "if" Name ":=" Expression "?" [ "then" Statement ]
   *                                            [ "else" Statement ] */
  if (at_id(p) && tk(p, 1) == T_OP_DEFINE) {
    Node *name = piname(p);
    adv(p);                               /* := */
    Node *cond = parse_no_q_expr(p);
    if (!cond) return NULL;
    if (!at(p, T_OP_QMARK)) { err(p, "expected '?' in the checked if"); return NULL; }
    Token qt = curtok(p);
    adv(p);
    Node *then = NULL, *els = NULL;
    if (at(p, T_KW_THEN)) {
      adv(p);
      skip_sep(p);
      then = parse_statement(p);
      if (!then) return NULL;
    }
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      if (at(p, T_KW_ELSE)) {
        adv(p);
        skip_sep(p);
        els = parse_statement(p);
        if (!els) return NULL;
      } else {
        restore_cur(p, save);     /* leave the separator for the caller */
      }
    }
    Node *n = pnode(p, N_CHECK_IF, qt);
    ppush(p, n, name);
    ppush(p, n, cond);
    ppush(p, n, then);
    ppush(p, n, els);
    return n;
  }
  Node *cond = parse_no_q_expr(p);
  if (!cond) return NULL;
  if (at(p, T_OP_QMARK)) {
    /* CheckedIf = "if" Expression "?" "then" Statement [ "else" Statement ] */
    Token qt = curtok(p);
    adv(p);
    if (!at(p, T_KW_THEN)) { err(p, "expected 'then' after '?' in the checked if"); return NULL; }
    adv(p);
    skip_sep(p);
    Node *then = parse_statement(p);
    if (!then) return NULL;
    Node *els = NULL;
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      if (at(p, T_KW_ELSE)) {
        adv(p);
        skip_sep(p);
        els = parse_statement(p);
        if (!els) return NULL;
      } else {
        restore_cur(p, save);     /* leave the separator for the caller */
      }
    }
    Node *n = pnode(p, N_CHECK_IF, qt);
    ppush(p, n, NULL);
    ppush(p, n, cond);
    ppush(p, n, then);
    ppush(p, n, els);
    return n;
  }
  if (at(p, T_KW_THEN)) {
    adv(p);
    skip_sep(p);
    Node *then = parse_statement(p);
    if (!then) return NULL;
    Node *els = NULL;
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      if (at(p, T_KW_ELSE)) {
        adv(p);
        skip_sep(p);
        els = parse_statement(p);
        if (!els) return NULL;
      } else {
        restore_cur(p, save);     /* leave the separator for the caller */
      }
    }
    Node *n = pnode(p, N_IF, t);
    ppush(p, n, cond);
    ppush(p, n, then);
    ppush(p, n, els);
    return n;
  }
  if (at(p, T_LBRACE)) {
    Node *blk = parse_block(p);
    if (!blk) return NULL;
    Node *els = NULL;
    {
      size_t save;
      save_cur(p, &save);
      skip_sep(p);
      if (at(p, T_KW_ELSE)) {
        adv(p);
        skip_sep(p);
        if (at(p, T_LBRACE)) {
        els = parse_block(p);
      } else {
        els = parse_statement(p);
      }
      if (!els) return NULL;
      } else {
        restore_cur(p, save);     /* leave the separator for the caller */
      }
    }
    Node *n = pnode(p, N_IF, t);
    ppush(p, n, cond);
    ppush(p, n, blk);
    ppush(p, n, els);
    return n;
  }
  err(p, "expected 'then', '{', or '?' after the if condition");
  return NULL;
}

static Node *parse_block_members(Parser *p, Node *n) {
  /*
   * The guarded-region machinery (§6.8): statements run flat in the block;
   * a `try` starts a region that extends to `catch Name` — everything
   * between is the region, everything after `catch` to the block's end is
   * the handler tail.  At most one catch per block.
   */
  Node *regions = NULL;   /* N_LIST of region statements */
  Node *catch_name = NULL;
  Node *handlers = NULL;  /* N_LIST of handler statements */
  int in_region = 0, in_handler = 0;

  for (;;) {
    skip_sep(p);
    if (at(p, T_RBRACE)) break;
    if (at(p, T_EOF)) { err(p, "unterminated block"); return NULL; }
    if (at(p, T_KW_CATCH)) {
      if (!in_region) { err(p, "`catch` without a preceding `try`"); return NULL; }
      if (in_handler) { err(p, "at most one `catch` per block"); return NULL; }
      adv(p);
      catch_name = piname(p);
      if (!catch_name) return NULL;
      in_handler = 1;
      handlers = pnlist(p);
      continue;
    }
    Node *st = parse_statement(p);
    if (!st) return NULL;
    if (st->k == N_TRY) {
      if (in_handler) { err(p, "`try` inside the handler tail"); return NULL; }
      if (!in_region) {
        in_region = 1;
        regions = pnlist(p);
      }
      ppush(p, regions, st);
    } else if (in_handler) {
      ppush(p, handlers, st);
    } else if (in_region) {
      ppush(p, regions, st);
    } else {
      ppush(p, n, st);
    }
    if (at(p, T_SEP) || at(p, T_RBRACE) || at(p, T_EOF)) continue;
    err(p, "expected a separator between statements");
    return NULL;
  }

  if (in_region) {
    if (!in_handler) { err(p, "guarded region without `catch`"); return NULL; }
    Node *r = pnode(p, N_REGION, regions->tok);
    ppush(p, r, regions);
    ppush(p, r, catch_name);
    ppush(p, r, handlers);
    ppush(p, n, r);
  }
  adv(p);                                   /* } */
  return n;
}

static Node *parse_block(Parser *p) {
  Token t = curtok(p);
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnode(p, N_BLOCK, t);
  return parse_block_members(p, n);
}

static Node *parse_statement(Parser *p) {
  TokKind k = tk(p, 0);
  switch (k) {
  case T_KW_IF: return parse_if(p);
  case T_KW_LOOP: return parse_loop(p);
  case T_KW_MATCH: return parse_match(p);
  case T_KW_SELECT: return parse_select(p);
  case T_KW_TRY: {
    Token t = curtok(p);
    adv(p);
    Node *s = parse_statement(p);
    if (!s) return NULL;
    return punary(p, N_TRY, t, s);
  }
  case T_KW_RETURN: {
    Token t = curtok(p);
    adv(p);
    if (at(p, T_SEP) || at(p, T_RBRACE) || at(p, T_EOF)) {
      return punary(p, N_RETURN, t, NULL);
    }
    if (at(p, T_KW_CATCH) || at(p, T_KW_THEN) || at(p, T_KW_ELSE) ||
        at(p, T_OP_FATARROW)) {
      return punary(p, N_RETURN, t, NULL);
    }
    Node *e = parse_expr(p);
    if (!e) return NULL;
    Node *n = pnode(p, N_RETURN, t);
    ppush(p, n, e);
    return n;
  }
  case T_KW_DEFER: {
    Token t = curtok(p);
    adv(p);
    Node *b;
    if (at(p, T_LBRACE)) b = parse_block(p);
    else b = parse_statement(p);
    if (!b) return NULL;
    return punary(p, N_DEFER, t, b);
  }
  case T_KW_BREAK: case T_KW_CONTINUE: case T_KW_YIELD: {
    Token t = curtok(p);
    adv(p);
    Node *label = NULL;
    if (at_id(p)) {
      label = piname(p);
      if (!label) return NULL;
    }
    return punary(p, N_TRANSFER, t, label);
  }
  case T_KW_SPAWN: {
    Token t = curtok(p);
    adv(p);
    Node *e = parse_postfix(p);
    if (!e) return NULL;
    return punary(p, N_SPAWN, t, e);
  }
  case T_LBRACE:
    return parse_block(p);
  case T_OP_ARROW:
    err(p, "`->` has no syntax role (open item §9)");
    return NULL;
  case T_OP_INC:
    err(p, "`++` is not in the language — use `i += 1`");
    return NULL;
  case T_OP_DEC:
    err(p, "`--` is not in the language — use `i -= 1`");
    return NULL;
  default:
    break;
  }
  /* A labeled loop: `Name ":" "loop" …` */
  if (at_id(p) && tk(p, 1) == T_COLON && tk(p, 2) == T_KW_LOOP) {
    Token t = curtok(p);
    Node *label = piname(p);
    adv(p);                               /* : */
    Node *lp = parse_loop(p);
    if (!lp) return NULL;
    Node *n = pnode(p, N_LABEL, t);
    ppush(p, n, label);
    ppush(p, n, lp);
    return n;
  }
  /* VariableDecl: `x := …`, `x int …`, `x const float …`, `x [10]int …` */
  if (at_id(p)) {
    if (tk(p, 1) == T_OP_DEFINE) return parse_var_decl(p);
    if (type_start_kind(tk(p, 1)) || tk(p, 1) == T_KW_CONST) {
      /* `x * T` / `x & T` double as pointer/slot decls or as a plain
       * multiplication / bitand statement (`x * 2`). A literal can never
       * start a type, so that case is an expression statement. */
      if ((tk(p, 1) == T_OP_STAR || tk(p, 1) == T_OP_AMP) &&
          (tk(p, 2) == T_NUM || tk(p, 2) == T_CHAR || tk(p, 2) == T_STR))
        return parse_expr_stmt(p);
      return parse_var_decl(p);
    }
    if (tk(p, 1) == T_LBRACKET) {
      /* `buf []T` vs `a[i] = …`: an array-typed decl only if a type follows
       * the closing bracket. */
      size_t save;
      save_cur(p, &save);
      p->quiet++;
      Node *nm = piname(p);
      Node *ty = nm ? parse_type(p) : NULL;
      p->quiet--;
      if (ty) {
        restore_cur(p, save);
        return parse_var_decl(p);
      }
      restore_cur(p, save);
    }
  }
  return parse_expr_stmt(p);
}

/* ---------------- declarations (§4) ---------------- */

static Node *parse_func_tail(Parser *p, Node *qname) {
  /* after the name: "func" [TypeParams] "(" [ParameterList] ")"
   *                [Type] [Directive*] [FuncBody] */
  Token t = qname->tok;
  if (!at(p, T_KW_FUNC)) { err(p, "expected 'func'"); return NULL; }
  adv(p);
  Node *tp = NULL;
  if (at(p, T_LBRACKET)) {
    tp = parse_typeparams(p);
    if (!tp) return NULL;
  }
  Node *params = parse_parameter_list(p);
  if (!params) return NULL;
  Node *ret = NULL;
  if (at_type_start(p)) {
    ret = parse_type(p);
    if (!ret) return NULL;
  }
  Node *dirs = NULL;
  if (at(p, T_HASH)) {
    dirs = pnlist(p);
    while (at(p, T_HASH)) {
      Node *d = parse_directive(p);
      if (!d) return NULL;
      ppush(p, dirs, d);
    }
  }
  /* A body may follow on the next line (witness: `… #compiler.inline()`
   * newline `do …`).  Only a `=`/`do` on the other side of the separators
   * switches on the body; otherwise the decl ends here. */
  Node *body = NULL;
  {
    size_t save;
    save_cur(p, &save);
    skip_sep(p);
    int has_body = at(p, T_OP_ASSIGN) || at(p, T_KW_DO);
    restore_cur(p, save);
    if (has_body) {
      skip_sep(p);
      if (at(p, T_OP_ASSIGN)) {
        adv(p);
        if (at(p, T_LBRACE)) {
          body = parse_block(p);
        } else {
          body = parse_expr(p);
        }
      } else {
        adv(p);                          /* do */
        skip_sep(p);                     /* `do` may end the line */
        body = parse_statement(p);
      }
      if (!body) return NULL;
    }
  }
  Node *n = pnode(p, N_FUNC_DECL, t);
  ppush(p, n, qname);
  ppush(p, n, tp);
  ppush(p, n, params);
  ppush(p, n, ret);
  ppush(p, n, dirs);
  ppush(p, n, body);
  return n;
}

static Node *parse_func_decl(Parser *p) {
  /* on the leading name (or the composed `infix_operator <op>`) */
  if (same_text(p, 0, "infix_operator") && operator_kind(tk(p, 1)) &&
      adjacent(p, 0) && tk(p, 2) == T_KW_FUNC) {
    /* OperatorName = the literal composed token `infix_operator <op>` */
    Token t = curtok(p);
    t.len = tok(p, 1).start + tok(p, 1).len - t.start;
    Node *name = pnode(p, N_NAME, t);
    adv(p);
    adv(p);
    return parse_func_tail(p, name);
  }
  Node *qname = piname(p);
  if (!qname) return NULL;
  if (at(p, T_DOT)) {
    adv(p);
    Node *second = piname(p);
    if (!second) return NULL;
    Node *qn = pnode(p, N_QNAME, qname->tok);
    ppush(p, qn, qname);
    ppush(p, qn, second);
    qname = qn;
  }
  return parse_func_tail(p, qname);
}

static Node *parse_struct_decl(Parser *p) {
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  if (at(p, T_KW_CONST)) adv(p);
  if (!at(p, T_KW_STRUCT)) { err(p, "expected 'struct'"); return NULL; }
  adv(p);
  Node *tp = NULL;
  if (at(p, T_LBRACKET)) {
    tp = parse_typeparams(p);
    if (!tp) return NULL;
  }
  Node *body = NULL;
  if (at(p, T_OP_ASSIGN)) {
    adv(p);
    body = parse_struct_body(p);
  } else if (at(p, T_KW_DO)) {
    adv(p);
    skip_sep(p);
    body = parse_statement(p);
  } else if (at(p, T_HASH)) {
    body = parse_directive(p);
  } else {
    err(p, "expected '=', 'do', or a directive after 'struct'");
    return NULL;
  }
  if (!body) return NULL;
  Node *n = pnode(p, N_STRUCT_DECL, t);
  ppush(p, n, name);
  ppush(p, n, tp);
  ppush(p, n, body);
  return n;
}

static Node *parse_enum_decl(Parser *p) {
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  if (!at(p, T_KW_ENUM)) { err(p, "expected 'enum'"); return NULL; }
  adv(p);
  Node *body = NULL;
  if (at(p, T_OP_ASSIGN)) {
    adv(p);
    body = parse_enum_body(p);
  } else if (at(p, T_KW_DO)) {
    adv(p);
    skip_sep(p);
    body = parse_statement(p);
  } else {
    err(p, "expected '=' or 'do' after the enum name");
    return NULL;
  }
  if (!body) return NULL;
  Node *n = pnode(p, N_ENUM_DECL, t);
  ppush(p, n, name);
  ppush(p, n, body);
  return n;
}

static Node *parse_error_body(Parser *p) {
  /* ErrorBody = "{" { FieldDecl } "}" — struct-shaped, restricted payloads */
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated error body"); return NULL; }
    Node *f = parse_field_decl(p);
    if (!f) return NULL;
    ppush(p, n, f);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between error fields");
    return NULL;
  }
  adv(p);                                 /* } */
  return n;
}

static Node *parse_error_decl(Parser *p) {
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  if (!at(p, T_KW_ERROR)) { err(p, "expected 'error'"); return NULL; }
  adv(p);
  Node *body;
  if (at(p, T_OP_ASSIGN)) {
    adv(p);
    body = parse_error_body(p);
  } else if (at(p, T_LBRACE)) {
    adv(p);
    if (!at(p, T_RBRACE)) { err(p, "expected '}' (the empty error kind)"); return NULL; }
    adv(p);
    body = pnlist(p);
  } else {
    err(p, "expected '=' or '{}' after the error name");
    return NULL;
  }
  if (!body) return NULL;
  Node *n = pnode(p, N_ERROR_DECL, t);
  ppush(p, n, name);
  ppush(p, n, body);
  return n;
}

static Node *parse_interface_body(Parser *p) {
  /* InterfaceBody = "{" { MethodSig } "}" */
  if (!at(p, T_LBRACE)) { err(p, "expected '{'"); return NULL; }
  adv(p);
  Node *n = pnlist(p);
  skip_sep(p);
  while (!at(p, T_RBRACE)) {
    if (at(p, T_EOF)) { err(p, "unterminated interface body"); return NULL; }
    Node *m = parse_method_sig(p);
    if (!m) return NULL;
    ppush(p, n, m);
    if (at(p, T_SEP)) { skip_sep(p); continue; }
    if (at(p, T_RBRACE)) break;
    err(p, "expected a separator between method signatures");
    return NULL;
  }
  adv(p);                                 /* } */
  return n;
}

static Node *parse_interface_decl(Parser *p) {
  Token t = curtok(p);
  Node *name = piname(p);
  if (!name) return NULL;
  if (!at(p, T_KW_INTERFACE)) { err(p, "expected 'interface'"); return NULL; }
  adv(p);
  Node *tp = NULL;
  if (at(p, T_LBRACKET)) {
    tp = parse_typeparams(p);
    if (!tp) return NULL;
  }
  if (!at(p, T_OP_ASSIGN)) {
    err(p, "expected '=' — interface bodies are `= { … }` only (C23)");
    return NULL;
  }
  adv(p);
  Node *body = parse_interface_body(p);
  if (!body) return NULL;
  Node *n = pnode(p, N_INTERFACE_DECL, t);
  ppush(p, n, name);
  ppush(p, n, tp);
  ppush(p, n, body);
  return n;
}

/* ---------------- FuncDecl-vs-FunctionType dispatch ---------------- */

/* Scan from token index i (a `func` keyword) through the parameter list and
 * classify it for FuncDecl-vs-FunctionType dispatch:
 *   1  ⇒ some parameter carries a name (⇒ FuncDecl),
 *  -1  ⇒ the list is empty (`main func () int …` — a FuncDecl),
 *   0  ⇒ all-bare types (`func (const T, const T)` — a FunctionType,
 *        so the declaration is a VariableDecl). */
static int param_state_at(Parser *p, size_t i) {
  if (p->toks[i].kind == T_KW_FUNC) i++;
  if (p->toks[i].kind == T_LBRACKET) {   /* skip [TypeParams] */
    int depth = 0;
    for (;; i++) {
      TokKind k = p->toks[i].kind;
      if (k == T_EOF) return 0;
      if (k == T_LBRACKET) depth++;
      else if (k == T_RBRACKET && --depth == 0) { i++; break; }
    }
  }
  if (p->toks[i].kind != T_LPAREN) return 0;
  int depth = 0, saw = 0;
  for (;; i++) {
    TokKind k = p->toks[i].kind;
    if (k == T_EOF) return 0;
    if (k == T_LPAREN) { depth++; continue; }
    if (k == T_RPAREN) {
      if (--depth == 0) return saw ? 0 : -1;
      continue;
    }
    if (depth == 1 && k == T_ID) {
      TokKind nx = p->toks[i + 1].kind;
      if (nx == T_KW_CONST || nx == T_OP_DEFINE || type_start_kind(nx))
        return 1;
      saw = 1;
    }
  }
}

/* ---------------- top level ---------------- */

static Node *parse_module(Parser *p) {
  Token t = curtok(p);
  if (!at(p, T_KW_MODULE)) { err(p, "expected 'module'"); return NULL; }
  adv(p);
  Node *name = piname(p);
  if (!name) return NULL;
  Node *n = pnode(p, N_MODULE, t);
  ppush(p, n, name);
  return n;
}

static Node *parse_toplevel(Parser *p) {
  /* TopLevel = ImportBlock | ModuleDecl | Declaration | Statement */
  if (at(p, T_KW_MODULE)) return parse_module(p);
  if (at_id(p)) {
    TokKind k1 = tk(p, 1);
    if (k1 == T_KW_STRUCT) return parse_struct_decl(p);
    if (k1 == T_KW_ENUM) return parse_enum_decl(p);
    if (k1 == T_KW_ERROR) {
      /* `Name error = …` / `Name error { }` ⇒ ErrorDecl; a bare
       * `Name error` is a VariableDecl of the builtin type. */
      TokKind k2 = tk(p, 2);
      if (k2 == T_OP_ASSIGN || k2 == T_LBRACE) return parse_error_decl(p);
      return parse_statement(p);
    }
    if (k1 == T_KW_INTERFACE) return parse_interface_decl(p);
    if (k1 == T_KW_FUNC) {
      /* `name func (…)`: named (or empty) params ⇒ FuncDecl; all-bare
       * params ⇒ a VariableDecl whose type is a FunctionType. */
      if (param_state_at(p, p->cur + 1) != 0) return parse_func_decl(p);
      return parse_statement(p);          /* `less func (const T, const T) bool */
    }
    if (k1 == T_DOT && tk(p, 2) == T_ID && tk(p, 3) == T_KW_FUNC)
      return parse_func_decl(p);          /* dotted: static constructor */
    if (same_text(p, 0, "infix_operator") && operator_kind(k1) &&
        adjacent(p, 0) && tk(p, 2) == T_KW_FUNC)
      return parse_func_decl(p);
    return parse_statement(p);
  }
  return parse_statement(p);
}

static Node *parse_file(Parser *p) {
  Token t = curtok(p);
  Node *file = pnode(p, N_FILE, t);
  skip_sep(p);
  while (!at(p, T_EOF)) {
    Node *item;
    if (at(p, T_HASH)) {
      item = parse_import_block(p);
    } else {
      item = parse_toplevel(p);
    }
    if (!item) { p->cur = p->n - 1; break; }
    ppush(p, file, item);
    int had_sep = at(p, T_SEP);
    skip_sep(p);
    if (at(p, T_EOF)) break;
    if (!had_sep) {
      err(p, "expected a separator between top-level items");
      p->cur = p->n - 1;
      break;
    }
  }
  return file;
}

/* ---------------- driver ---------------- */

static int fill_tokens(Parser *p, const char *src) {
  Lexer lx;
  lexer_init(&lx, src);
  for (;;) {
    Token t = lexer_next(&lx);
    if (p->n == p->cap) {
      size_t nc = p->cap ? p->cap * 2 : 4096;
      Token *nt = realloc(p->toks, nc * sizeof(Token));
      if (!nt) {
        snprintf(p->msg, sizeof p->msg, "out of memory buffering tokens");
        return -1;
      }
      p->toks = nt;
      p->cap = nc;
    }
    p->toks[p->n++] = t;
    if (t.kind == T_UNDEF) {
      snprintf(p->msg, sizeof p->msg, "lexer: %s (line %d, col %d)",
               lx.err, t.line, t.col);
      return -1;
    }
    if (t.kind == T_EOF) return 0;
  }
}

int parser_run(Parser *p, const char *src, Node **out) {
  memset(p, 0, sizeof *p);
  if (fill_tokens(p, src) != 0) return -1;
  Node *file = parse_file(p);
  if (p->failed) return -1;
  *out = file;
  return 0;
}

/* ---------------- dump ---------------- */

const char *node_kind_name(NodeKind k) {
  static const char *const names[N_KIND_COUNT] = {
    "FILE", "LIST", "MODULE", "IMPORT", "DIRECTIVE",
    "VAR", "STRUCT_DECL", "ENUM_DECL", "ERROR_DECL", "INTERFACE_DECL",
    "FUNC_DECL", "QNAME", "PARAM", "TYPEPARAM", "TYPEARGS", "TYPEARG",
    "FIELD", "METHOD", "ENUM_MEMBER", "CONFORM",
    "TFUND", "TNAMED", "TARRAY", "TVIEW", "TFUNC", "TCONST", "TSHAPE",
    "INIT", "INITITEM", "STRUCT_LIT", "ANON_STRUCT", "FUNC_EXPR", "TRAILING",
    "BLOCK", "IF", "CHECK_IF", "LOOP", "LABEL", "CFOR", "IN_CLAUSE",
    "CHECK_IN", "MATCH", "MATCH_ARM", "SELECT", "SELECT_ARM", "TRY",
    "REGION", "RETURN", "DEFER", "TRANSFER", "SPAWN", "SEND", "SWAP",
    "ASSIGN", "EXPR_STMT",
    "BIN", "UNARY", "RECV", "INDEX", "SLICE", "CALL", "MEMBER", "UNWRAP",
    "NAME", "LIT", "IS",
  };
  if ((int)k < 0 || (int)k >= N_KIND_COUNT) return "?";
  return names[k];
}

void node_dump(const Node *n, int depth, FILE *out) {
  if (!n) return;
  fprintf(out, "%*s%s", depth * 2, "", node_kind_name(n->k));
  Token t = n->tok;
  if (t.start && t.len) {
    fputs(" `", out);
    fwrite(t.start, 1, t.len, out);
    fputc('\'', out);
  }
  fputc('\n', out);
  for (int i = 0; i < n->n; i++) node_dump(n->ch[i], depth + 1, out);
}