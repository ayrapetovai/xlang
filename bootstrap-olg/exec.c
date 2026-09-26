#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#include "exec.h"

/* ================= values ================= */

typedef enum {
  V_VOID, V_INT, V_UINT, V_FLOAT, V_BOOL, V_CHAR, V_BYTE,
  V_STR, V_ARR, V_REC, V_FUNC, V_TYPE, V_ENUM, V_RANGE, V_ERR,
  V_PTR
} ValKind;

/* fundamental-type tags for V_TYPE values */
enum { TY_BYTE, TY_CHAR, TY_INT, TY_UINT, TY_FLOAT, TY_BOOL,
       TY_STRING, TY_BYTES, TY_ERROR };

typedef struct Value Value;
typedef struct Env Env;
typedef struct Exec Exec;

typedef enum { F_BUILTIN, F_USER } FuncKind;
typedef void (*BuiltinFn)(Exec *e, Value *args, int n, Value *out, int tag);

typedef struct {
  FuncKind kind;
  int tag;                /* builtin discriminator */
  const char *name;
  Node *params, *body;    /* F_USER */
  Env *closure;           /* F_USER — captured environment */
  BuiltinFn fn;           /* F_BUILTIN */
} Func;

typedef struct Field Field;   /* forward: `rec` holds `Field *` */

struct Value {
  ValKind k;
  union {
    int64_t i;
    uint64_t u;
    double f;
    int b;
    uint32_t c32;         /* char */
    uint8_t by;           /* byte */
    int ty;               /* V_TYPE: TY_* */
    struct { const char *s; size_t n; } str;
    struct { Value *els; size_t n, cap; } arr;
    struct { Field *fs; size_t n; const char *tyname; } rec;
    struct { const char *name; int tag; } en;
    struct { int64_t lo, hi; int desc; int incl; } rng;
    struct { const char *name; struct { const char *s; size_t n; } msg; } err;
    struct { const char *nm; Func f; } fn;
    Value *cell;          /* V_PTR — reserved; identity for now */
  };
};

/* A record field: name + value cell (records share their field array). */
struct Field { const char *nm; Value v; };

/* ================= environment ================= */

typedef struct Binding { const char *nm; Value v; } Binding;

struct Env {
  Env *parent;
  Binding *b;
  size_t n, cap;
  Arena *ar;
};

/* ================= exec machinery ================= */

typedef enum { FL_NONE, FL_RETURN, FL_BREAK, FL_CONTINUE } Flow;

/* A catch frame lives in the exec arena (not on the C stack): after a
 * longjmp the automatic locals of the skipped frames are gone, but the

 * raise site and the catch site both reach the frame through e->catch_, so
 * restore state is read from here. */
typedef struct Catch {
  jmp_buf buf;
  struct Catch *prev;
  Env *saved_cur;            /* e->cur at the try */
  Node *regs, *catch_name, *handlers;
  size_t saved_nd;           /* defer-list length at the try */
  int saved_depth;           /* recursion counter at the try */
} Catch;

struct Exec {
  Arena *ar;               /* heap-allocated: see the struct comment on errb */
  Env *global, *cur, *fnframe;
  Flow flow;
  const char *label;         /* label of a pending break/continue */
  Value ret;                 /* FL_RETURN payload */
  Value exc;                 /* raised error value */
  Catch *catch_;             /* innermost catch frame */
  /* region/function-scoped defers: `nd` positions a window in the list,
   * and catch frames / function calls remember the window start to drain
   * defers on normal exit. */
  Node **defers;
  size_t nd, capd;
  /* struct / enum registries */
  struct Styp { const char *name; Node *fields; } *styp;
  size_t nsty, capsty;
  struct Etyp { const char *name; Node *members; } *etyp;
  size_t nety, capety;
  /* Fatal diagnostic + abort frame.  msg/line/col live in a separate
   * arena block whose pointer (`errb`) is assigned once *before* the
   * setjmp and never reassigned: after `longjmp(e->abort, …)` the
   * automatic locals of exec_run — the function containing the setjmp —
   * are indeterminate (C11 §7.13.2.1), so the recovery path may only
   * read fields that never changed since setjmp.  Writing through `e`
   * from a deeper frame (vfail) would corrupt that guarantee, hence the
   * heap block. */
  int depth, depth_max;   /* recursion guard: eval/exec_stmt crossings */
  struct { char msg[512]; int line, col; } *errb;
  jmp_buf abort;
  const char *src;         /* source buffer (for token context lookups) */
};

/* ---------------- tiny helpers ---------------- */

static void *xalloc(Exec *e, size_t n) {
  return arena_alloc(e->ar, n);
}

static const char *ttext(const Node *n) {
  return n ? n->tok.start : "(?)";
}

/* Arena-owned NUL-terminated copy of a token slice. */
static const char *savestr(Exec *e, const char *s, size_t n) {
  char *p = xalloc(e, n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

/* ---------------- fatal diagnostics ---------------- */

static void vfail(Exec *e, const Node *n, const char *fmt, ...)
  __attribute__((format(printf, 3, 4), noreturn));

static void vfail(Exec *e, const Node *n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->errb->msg, sizeof e->errb->msg, fmt, ap);
  va_end(ap);
  e->errb->line = n ? n->line : 0;
  e->errb->col = n ? n->col : 0;
  longjmp(e->abort, 1);
}

/* ---------------- error values & unwrap ---------------- */

static Value err_value(Exec *e, const char *name, const char *msg) {
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_ERR;
  v.err.name = savestr(e, name, strlen(name));
  v.err.msg.s = savestr(e, msg, strlen(msg));
  v.err.msg.n = strlen(msg);
  return v;
}

static Value str_value(const char *s, size_t n) {
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_STR;
  v.str.s = s;
  v.str.n = n;
  return v;
}

/* Raise `err`: longjmp to the innermost catch, or die uncaught. */
static void raise(Exec *e, const Node *at, Value err) {
  e->exc = err;
  if (e->catch_) longjmp(e->catch_->buf, 1);
  {
    char buf[256];
    snprintf(buf, sizeof buf, "uncaught error %s: %.*s",
             err.k == V_ERR ? err.err.name : "?",
             (int)err.err.msg.n, err.err.msg.s);
    vfail(e, at, "%s", buf);
  }
}

/* `x!` — raise on a failure value. */
static void unwrap_hard(Exec *e, const Node *n, Value *v) {
  if (v->k == V_PTR && v->cell) *v = *v->cell;
  if (v->k == V_ERR) raise(e, n, *v);
}

/* `x?` — settle failure into an absent V_VOID (checked heads test it). */
static void unwrap_soft(Exec *e, Value *v) {
  if (v->k == V_ERR) {
    (void)e;
    v->k = V_VOID;
  }
}

/* ---------------- environment ops ---------------- */

static Env *env_new(Exec *e, Env *parent) {
  Env *env = xalloc(e, sizeof *env);
  memset(env, 0, sizeof *env);          /* b/n/cap must start zeroed */
  env->parent = parent;
  env->ar = e->ar;
  return env;
}

static void env_bind(Exec *e, Env *env, const char *nm, Value v) {
  if (env->n == env->cap) {
    size_t nc = env->cap ? env->cap * 2 : 8;
    Binding *nb = xalloc(e, nc * sizeof *nb);
    if (env->b) memcpy(nb, env->b, env->n * sizeof *nb);
    env->b = nb;
    env->cap = nc;
  }
  env->b[env->n].nm = nm;
  env->b[env->n].v = v;
  env->n++;
}

static int env_lookup(Env *env, const char *nm, Value *out) {
  for (Env *f = env; f; f = f->parent)
    for (size_t i = f->n; i-- > 0;)
      if (strcmp(f->b[i].nm, nm) == 0) { *out = f->b[i].v; return 1; }
  return 0;
}

/* ---------------- value helpers ---------------- */

static const char *kind_name(ValKind k);

static int val_truth(Exec *e, const Node *at, Value v) {
  switch (v.k) {
  case V_BOOL: return v.b;
  case V_INT: return v.i != 0;
  case V_UINT: return v.u != 0;
  case V_FLOAT: return v.f != 0.0;
  case V_CHAR: return v.c32 != 0;
  case V_BYTE: return v.by != 0;
  case V_STR: return v.str.n != 0;
  case V_PTR: return v.cell ? val_truth(e, at, *v.cell) : 0;
  case V_ERR: return 0;
  default:
    vfail(e, at, "a %s value is not usable as a condition", kind_name(v.k));
  }
}

/* Structural equality used by match patterns, `==`, and enum compares. */
static int val_eq(Exec *e, const Node *at, Value a, Value b) {
  switch (a.k) {
  case V_INT:  return b.k == V_INT  && a.i == b.i;
  case V_UINT: return b.k == V_UINT && a.u == b.u;
  case V_BOOL: return b.k == V_BOOL && a.b == b.b;
  case V_CHAR: return b.k == V_CHAR && a.c32 == b.c32;
  case V_BYTE: return b.k == V_BYTE && a.by == b.by;
  case V_FLOAT:
    return (b.k == V_FLOAT && a.f == b.f) ||
           (b.k == V_INT   && a.f == (double)b.i) ||
           (b.k == V_UINT  && a.f == (double)b.u);
  case V_STR:
    return (b.k == V_STR && a.str.n == b.str.n &&
            memcmp(a.str.s, b.str.s, a.str.n) == 0);
  case V_ENUM:
    return b.k == V_ENUM && strcmp(a.en.name, b.en.name) == 0 &&
           a.en.tag == b.en.tag;
  case V_PTR: return b.k == V_PTR && a.cell == b.cell;
  case V_ERR:
    return b.k == V_ERR && strcmp(a.err.name, b.err.name) == 0 &&
           a.err.msg.n == b.err.msg.n &&
           memcmp(a.err.msg.s, b.err.msg.s, a.err.msg.n) == 0;
  default:
    vfail(e, at, "cannot compare %s values with `==`", kind_name(a.k));
  }
}

static void val_print(FILE *f, Value v) {
  switch (v.k) {
  case V_VOID: return;
  case V_INT: fprintf(f, "%lld", (long long)v.i); return;
  case V_UINT: fprintf(f, "%llu", (unsigned long long)v.u); return;
  case V_FLOAT: fprintf(f, "%g", v.f); return;
  case V_BOOL: fputs(v.b ? "true" : "false", f); return;
  case V_CHAR: fputc((int)(v.c32 < 0x80 ? v.c32 : '?'), f); return;
  case V_BYTE: fprintf(f, "%u", (unsigned)v.by); return;
  case V_STR: fwrite(v.str.s, 1, v.str.n, f); return;
  case V_ARR:
    fputc('[', f);
    for (size_t i = 0; i < v.arr.n; i++) {
      if (i) fputs(", ", f);
      val_print(f, v.arr.els[i]);
    }
    fputc(']', f);
    return;
  case V_REC:
    fputc('{', f);
    for (size_t i = 0; i < v.rec.n; i++) {
      if (i) fputs(", ", f);
      fputs(v.rec.fs[i].nm, f);
      fputc('=', f);
      val_print(f, v.rec.fs[i].v);
    }
    fputc('}', f);
    return;
  case V_ENUM: fputs(v.en.name, f); return;
  case V_TYPE:
    fputs(v.ty == TY_ERROR ? "error" :
          v.ty == TY_STRING ? "string" : v.ty == TY_BYTES ? "bytes" :
          v.ty == TY_INT ? "int" : v.ty == TY_UINT ? "uint" :
          v.ty == TY_FLOAT ? "float" : v.ty == TY_BOOL ? "bool" :
          v.ty == TY_CHAR ? "char" : "byte", f);
    return;
  case V_RANGE:
    fprintf(f, "%lld%s%lld", (long long)v.rng.lo,
            v.rng.desc ? (v.rng.lo == v.rng.hi ? ">..=" : ">..<")
                       : (v.rng.lo == v.rng.hi ? "..=" : "..<"),
            (long long)v.rng.hi);
    return;
  case V_ERR:
    fprintf(f, "error %s: %.*s", v.err.name,
            (int)v.err.msg.n, v.err.msg.s);
    return;
  case V_FUNC: fprintf(f, "func %s", v.fn.nm); return;
  case V_PTR: if (v.cell) val_print(f, *v.cell); return;
  }
}

static const char *kind_name(ValKind k) {
  switch (k) {
  case V_VOID: return "void";
  case V_INT: return "int";
  case V_UINT: return "uint";
  case V_FLOAT: return "float";
  case V_BOOL: return "bool";
  case V_CHAR: return "char";
  case V_BYTE: return "byte";
  case V_STR: return "string";
  case V_ARR: return "array";
  case V_REC: return "record";
  case V_FUNC: return "func";
  case V_TYPE: return "type";
  case V_ENUM: return "enum";
  case V_RANGE: return "range";
  case V_ERR: return "error";
  case V_PTR: return "pointer";
  }
  return "?";
}

static Value num_add(Exec *e, const Node *at, Value a, Value b, int op);
static Value num_cmp(Exec *e, const Node *at, Value a, Value b, int op);
static Value bin_bitsh(Exec *e, const Node *at, Value a, Value b, TokKind k);
static Value bin_bit(Exec *e, const Node *at, Value a, Value b, TokKind k);
static int range_desc(Exec *e, const Node *n);

/* ================= literal parsing ================= */

/* Strip `_` separators into a stack buffer; rejects an empty result. */
static const char *denumerate(Exec *e, const Node *n, size_t *out_n) {
  const char *s = n->tok.start;
  size_t L = n->tok.len;
  char *p = xalloc(e, L + 1);
  size_t k = 0;
  for (size_t i = 0; i < L; i++)
    if (s[i] != '_') p[k++] = s[i];
  p[k] = '\0';
  *out_n = k;
  return p;
}

static int has_prefix(const char *s, size_t n, const char *pfx) {
  size_t pl = strlen(pfx);
  return n >= pl && memcmp(s, pfx, pl) == 0;
}

/* ================= string interpolation ================= */

/* "…%L{expr}…" — GRAMMAR §2.3 (decision C26).  The lexer keeps the whole
 * literal as one raw T_STR token (backslashes preserved), so the
 * interpreter splits it at eval time: backslash escapes use the minimal
 * escape set (decision C23, shared with char literals), and each format
 * spec's expression is re-parsed by the same parser into the exec arena
 * (`parser_expr_from_text`) and evaluated immediately.  Format letters in
 * scope: `s` plain, `q` quoted (JSON-ish), `d`/`n` decimal integer,
 * `f` float, `b` bool.  Unknown letters and a bare `%` stay literal (the
 * letter set is open in GRAMMAR §2.3; no escaping use is witnessed). */

static void eval(Exec *e, Node *n, Value *out);   /* forward: guard wrapper */

/* Growable byte buffer backed by the exec arena.  Re-growth copies into a
 * fresh block and leaves the old one as arena garbage — consistent with
 * the arena's free-nothing-until-the-end model. */
typedef struct Sb {
  char *s;
  size_t n, cap;
} Sb;

static void sb_put(Exec *e, Sb *b, const char *s, size_t n) {
  if (b->n + n > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 64;
    while (nc < b->n + n) nc *= 2;
    char *nb = xalloc(e, nc);
    if (b->n) memcpy(nb, b->s, b->n);
    b->s = nb;
    b->cap = nc;
  }
  memcpy(b->s + b->n, s, n);
  b->n += n;
}

static void sb_putc(Exec *e, Sb *b, char c) { sb_put(e, b, &c, 1); }

/* Render a value as text exactly the way `string.from`/`val_print` do, so
 * `%s{}` agrees with `println`.  A record/array recurses with val_print's
 * shapes; an error value renders its message (how README's `%s{e}`
 * usages read — the debug `error name: msg` form stays in val_print). */
static void fmt_str(Exec *e, Sb *b, Value x) {
  char buf[64];
  switch (x.k) {
  case V_VOID: return;
  case V_PTR: if (x.cell) fmt_str(e, b, *x.cell); return;
  case V_STR: sb_put(e, b, x.str.s, x.str.n); return;
  case V_INT: snprintf(buf, sizeof buf, "%lld", (long long)x.i); break;
  case V_UINT: snprintf(buf, sizeof buf, "%llu", (unsigned long long)x.u); break;
  case V_FLOAT: snprintf(buf, sizeof buf, "%g", x.f); break;
  case V_BOOL: sb_put(e, b, x.b ? "true" : "false", x.b ? 4 : 5); return;
  case V_CHAR: sb_putc(e, b, (char)(x.c32 < 0x80 ? x.c32 : '?')); return;
  case V_BYTE: snprintf(buf, sizeof buf, "%u", (unsigned)x.by); break;
  case V_ERR: sb_put(e, b, x.err.msg.s, x.err.msg.n); return;
  case V_ENUM: sb_put(e, b, x.en.name, strlen(x.en.name)); return;
  case V_TYPE: {
    const char *w = x.ty == TY_ERROR ? "error" : x.ty == TY_STRING ? "string" :
                    x.ty == TY_BYTES ? "bytes" : x.ty == TY_INT ? "int" :
                    x.ty == TY_UINT ? "uint" : x.ty == TY_FLOAT ? "float" :
                    x.ty == TY_BOOL ? "bool" : x.ty == TY_CHAR ? "char" : "byte";
    sb_put(e, b, w, strlen(w));
    return;
  }
  case V_RANGE: {
    const char *o = x.rng.desc ? (x.rng.lo == x.rng.hi ? ">..=" : ">..<")
                               : (x.rng.lo == x.rng.hi ? "..=" : "..<");
    snprintf(buf, sizeof buf, "%lld%s%lld", (long long)x.rng.lo, o,
             (long long)x.rng.hi);
    break;
  }
  case V_FUNC:
    sb_put(e, b, "func ", 5);
    sb_put(e, b, x.fn.nm, strlen(x.fn.nm));
    return;
  case V_ARR:
    sb_putc(e, b, '[');
    for (size_t j = 0; j < x.arr.n; j++) {
      if (j) sb_put(e, b, ", ", 2);
      fmt_str(e, b, x.arr.els[j]);
    }
    sb_putc(e, b, ']');
    return;
  case V_REC:
    sb_putc(e, b, '{');
    for (size_t j = 0; j < x.rec.n; j++) {
      if (j) sb_put(e, b, ", ", 2);
      sb_put(e, b, x.rec.fs[j].nm, strlen(x.rec.fs[j].nm));
      sb_putc(e, b, '=');
      fmt_str(e, b, x.rec.fs[j].v);
    }
    sb_putc(e, b, '}');
    return;
  }
  sb_put(e, b, buf, strlen(buf));
}

/* `%L{expr}` — format `x` for the format letter `verb`. */
static void fmt_val(Exec *e, const Node *at, Sb *b, int verb, Value x) {
  char buf[32];
  switch (verb) {
  case 's': fmt_str(e, b, x); return;
  case 'q': {   /* quoted: wrapper quotes + escapes `"` `\` \n \t \r \0 */
    Sb t = {0};
    fmt_str(e, &t, x);
    sb_putc(e, b, '"');
    for (size_t i = 0; i < t.n; i++) {
      switch (t.s[i]) {
      case '"': sb_put(e, b, "\\\"", 2); break;
      case '\\': sb_put(e, b, "\\\\", 2); break;
      case '\n': sb_put(e, b, "\\n", 2); break;
      case '\t': sb_put(e, b, "\\t", 2); break;
      case '\r': sb_put(e, b, "\\r", 2); break;
      case '\0': sb_put(e, b, "\\0", 2); break;
      default: sb_putc(e, b, t.s[i]);
      }
    }
    sb_putc(e, b, '"');
    return;
  }
  case 'd':
  case 'n':
    switch (x.k) {
    case V_INT: snprintf(buf, sizeof buf, "%lld", (long long)x.i); break;
    case V_UINT: snprintf(buf, sizeof buf, "%llu", (unsigned long long)x.u); break;
    case V_BYTE: snprintf(buf, sizeof buf, "%u", (unsigned)x.by); break;
    case V_CHAR: snprintf(buf, sizeof buf, "%u", (unsigned)x.c32); break;
    default:
      vfail(e, at, "cannot format %s with `%%%c` - use %%s",
            kind_name(x.k), verb);
    }
    sb_put(e, b, buf, strlen(buf));
    return;
  case 'f':
    if (x.k != V_FLOAT)
      vfail(e, at, "cannot format %s with `%%f` - use %%s", kind_name(x.k));
    snprintf(buf, sizeof buf, "%g", x.f);
    sb_put(e, b, buf, strlen(buf));
    return;
  case 'b':
    if (x.k != V_BOOL)
      vfail(e, at, "cannot format %s with `%%b`", kind_name(x.k));
    sb_put(e, b, x.b ? "true" : "false", x.b ? 4 : 5);
    return;
  }
}

static Value lit_value(Exec *e, const Node *n) {
  Value v;
  memset(&v, 0, sizeof v);
  size_t L = n->tok.len;
  switch (n->tok.kind) {
  case T_KW_TRUE: v.k = V_BOOL; v.b = 1; return v;
  case T_KW_FALSE: v.k = V_BOOL; v.b = 0; return v;
  case T_NUM: {
    const char *s = n->tok.start;
    size_t k = 0;
    const char *dig = denumerate(e, n, &k);
    if (has_prefix(s, L, "0x") || has_prefix(s, L, "0X")) {
      uint64_t u = strtoull(dig + 2, NULL, 16);
      if (u <= (uint64_t)INT64_MAX) { v.k = V_INT; v.i = (int64_t)u; }
      else { v.k = V_UINT; v.u = u; }
      return v;
    }
    if (has_prefix(s, L, "0b") || has_prefix(s, L, "0B")) {
      uint64_t u = 0;
      for (const char *q = dig + 2; *q; q++) u = u * 2 + (*q - '0');
      if (u <= (uint64_t)INT64_MAX) { v.k = V_INT; v.i = (int64_t)u; }
      else { v.k = V_UINT; v.u = u; }
      return v;
    }
    int is_float = 0;
    for (size_t i = 0; i < L; i++)
      if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') { is_float = 1; break; }
    if (is_float) { v.k = V_FLOAT; v.f = strtod(dig, NULL); return v; }
    errno = 0;
    uint64_t u = strtoull(dig, NULL, 10);
    if (u <= (uint64_t)INT64_MAX) { v.k = V_INT; v.i = (int64_t)u; }
    else { v.k = V_UINT; v.u = u; }
    if (errno) vfail(e, n, "integer literal out of range");
    return v;
  }
  case T_CHAR: {
    /* literal text: ' c ' or ' \x.. ' — minimal escape set (the full
     * escape set is still open, decision C23). */
    const char *s = n->tok.start;
    uint32_t c;
    if (L >= 3 && s[1] != '\\') {
      if (L == 3) c = (unsigned char)s[1];
      else { /* multi-byte: take the first byte */
        for (size_t i = 1; i < L; i++) if (s[i] == '\'') { c = s[1]; break; }
        c = (unsigned char)s[1];
      }
    } else if (L >= 4 && s[1] == '\\') {
      switch (s[2]) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case '0': c = 0; break;
      case '\\': c = '\\'; break;
      case '\'': c = '\''; break;
      case '"': c = '"'; break;
      case 'x': c = (uint32_t)strtoul(s + 3, NULL, 16); break;
      default: vfail(e, n, "unsupported escape in char literal");
      }
    } else {
      vfail(e, n, "malformed char literal");
    }
    v.k = V_CHAR;
    v.c32 = c;
    return v;
  }
  case T_STR: {
    /* escapes + format specs are processed here (GRAMMAR §2.3, decision
     * C26): the lexer keeps the whole literal as one raw token, so the
     * interpreter splits it at eval time. */
    const char *s = n->tok.start + 1;
    size_t inner = L >= 2 ? L - 2 : 0;
    /* fast path: plain text keeps the exact source slice (byte-identical) */
    if (!memchr(s, '\\', inner) && !memchr(s, '%', inner)) {
      v.k = V_STR; v.str.s = s; v.str.n = inner; return v;
    }
    Sb b = {0};
    size_t i = 0;
    while (i < inner) {
      char c = s[i];
      if (c == '\\') {
        if (i + 1 >= inner)
          vfail(e, n, "truncated escape in string literal");
        char esc = s[i + 1];
        unsigned char val;
        size_t adv = 2;
        switch (esc) {
        case 'n': val = '\n'; break;
        case 't': val = '\t'; break;
        case 'r': val = '\r'; break;
        case '0': val = 0; break;
        case '\\': val = '\\'; break;
        case '"': val = '"'; break;
        case '\'': val = '\''; break;
        case 'x': {
          size_t k = i + 2, hv = 0, nd = 0;
          while (k < inner && nd < 2 &&
                 isxdigit((unsigned char)s[k])) {
            unsigned char h = (unsigned char)tolower((unsigned char)s[k]);
            hv = hv * 16u + (isdigit(h) ? (unsigned)(h - '0')
                                        : (unsigned)(h - 'a' + 10));
            k++;
            nd++;
          }
          if (!nd) vfail(e, n, "`\\x` escape needs hex digits");
          val = (unsigned char)hv;
          adv = k - i;
          break;
        }
        default:
          vfail(e, n, "unsupported escape `\\%c` in string literal", esc);
        }
        sb_putc(e, &b, (char)val);
        i += adv;
      } else if (c == '%') {
        if (i + 2 < inner && strchr("sdnqfb", s[i + 1]) && s[i + 2] == '{') {
          int verb = (unsigned char)s[i + 1];
          /* find the matching `}` — brace-counted, skipping '…' char
           * literals so a `}` inside one does not close the spec (a "…"
           * string cannot occur here: a raw `"` would terminate the outer
           * literal at lex time, and `\"` leaves a `\` the sub-lexer
           * rejects — a lexer-design open item, see decision C26). */
          size_t k = i + 3, depth = 1;
          for (;;) {
            if (k >= inner)
              vfail(e, n, "unterminated format spec in string literal");
            char d = s[k];
            if (d == '"' || d == '\'') {
              size_t q = k + 1;
              while (q < inner && s[q] != d) {
                if (s[q] == '\\') q++;
                q++;
              }
              if (q >= inner)
                vfail(e, n, "unterminated string in format spec");
              k = q + 1;
              continue;
            }
            if (d == '{') depth++;
            else if (d == '}' && --depth == 0) break;
            k++;
          }
          size_t elen = k - (i + 3);
          /* copy the expression source into the exec arena: the lexer
           * needs a NUL terminator, and the parsed AST's token slices
           * point into this buffer, so it must outlive the eval below —
           * arena lifetime, never freed individually. */
          char *text = xalloc(e, elen + 1);
          memcpy(text, s + i + 3, elen);
          text[elen] = '\0';
          char ferr[512];
          Node *expr = parser_expr_from_text(text, e->ar, ferr, sizeof ferr);
          if (!expr)
            vfail(e, n, "bad format expression: %s", ferr);
          Value x;
          eval(e, expr, &x);
          fmt_val(e, n, &b, verb, x);
          i = k + 1;
        } else {
          sb_putc(e, &b, '%');   /* bare/unknown `%` stays literal (open) */
          i++;
        }
      } else {
        sb_putc(e, &b, c);
        i++;
      }
    }
    v.k = V_STR;
    v.str.s = b.s ? b.s : s;   /* empty build: still points inside src */
    v.str.n = b.n;
    return v;
  }
  default:
    vfail(e, n, "not a literal");
  }
}

/* ================= type-based zero values ================= */

static int fund_type_of(Exec *e, const Node *ty) {
  if (ty->k != N_TFUND) return -1;
  /* ttext is NOT NUL-terminated: compare a savestr'd copy, not the slice */
  const char *t = savestr(e, ttext(ty), ty->tok.len);
  if (strcmp(t, "int") == 0 || strcmp(t, "uint") == 0) return TY_INT;
  if (strcmp(t, "float") == 0) return TY_FLOAT;
  if (strcmp(t, "bool") == 0) return TY_BOOL;
  if (strcmp(t, "char") == 0) return TY_CHAR;
  if (strcmp(t, "byte") == 0) return TY_BYTE;
  if (strcmp(t, "string") == 0) return TY_STRING;
  if (strcmp(t, "bytes") == 0) return TY_BYTES;
  if (strcmp(t, "error") == 0) return TY_ERROR;
  return -1;
}

static Value zero_value(Exec *e, const Node *n) {
  Value v;
  memset(&v, 0, sizeof v);
  int ty = -1;
  if (n) {
    if (n->k == N_TSHAPE || n->k == N_TCONST || n->k == N_TVIEW)
      return zero_value(e, n->ch[0]);     /* shoot through shape/const/view */
    ty = fund_type_of(e, n);
  }
  switch (ty) {
  case TY_FLOAT: v.k = V_FLOAT; v.f = 0.0; return v;
  case TY_BOOL: v.k = V_BOOL; v.b = 0; return v;
  case TY_CHAR: v.k = V_CHAR; return v;
  case TY_BYTE: v.k = V_BYTE; return v;
  case TY_STRING: case TY_BYTES: v.k = V_STR; v.str.s = ""; return v;
  case TY_ERROR: return err_value(e, "", "");
  default: v.k = V_INT; v.i = 0; return v;   /* int / uint / fallback */
  }
}

/* ---------------- type registry ---------------- */

static const char *reg_struct(Exec *e, const Node *d) {
  const char *nm = savestr(e, ttext(d->ch[0]), d->ch[0]->tok.len);
  for (size_t i = 0; i < e->nsty; i++)
    if (strcmp(e->styp[i].name, nm) == 0) return nm;
  if (e->nsty == e->capsty) {
    size_t nc = e->capsty ? e->capsty * 2 : 8;
    struct Styp *ns = xalloc(e, nc * sizeof *ns);
    if (e->capsty) memcpy(ns, e->styp, e->nsty * sizeof *ns);
    e->styp = ns;
    e->capsty = nc;
  }
  e->styp[e->nsty].name = nm;
  /* body = the last N_LIST child: children are [name, typeparams?,
   * body, directives?], and a trailing directive must not be mistaken
   * for the field list (typeparams are N_TYPEPARAMS, never N_LIST). */
  Node *fields = NULL;
  for (int i = 0; i < d->n; i++)
    if (d->ch[i]->k == N_LIST) fields = d->ch[i];
  e->styp[e->nsty].fields = fields;
  e->nsty++;
  return nm;
}

static const char *reg_enum(Exec *e, const Node *d) {
  const char *nm = savestr(e, ttext(d->ch[0]), d->ch[0]->tok.len);
  for (size_t i = 0; i < e->nety; i++)
    if (strcmp(e->etyp[i].name, nm) == 0) return nm;
  if (e->nety == e->capety) {
    size_t nc = e->capety ? e->capety * 2 : 8;
    struct Etyp *ne = xalloc(e, nc * sizeof *ne);
    if (e->capety) memcpy(ne, e->etyp, e->nety * sizeof *ne);
    e->etyp = ne;
    e->capety = nc;
  }
  e->etyp[e->nety].name = nm;
  e->etyp[e->nety].members = d->ch[1];
  e->nety++;
  return nm;
}

static const struct Styp *find_struct(Exec *e, const char *nm) {
  for (size_t i = 0; i < e->nsty; i++)
    if (strcmp(e->styp[i].name, nm) == 0) return &e->styp[i];
  return NULL;
}

/* ---------------- evaluation & statements (fwd) ---------------- */

static void eval(Exec *e, Node *n, Value *out);
static Value exec_stmt(Exec *e, Node *n);
static Value exec_block(Exec *e, Node *blk);
static Value invoke_user(Exec *e, Func f, Value *args, int narg, const Node *at);

/* internal bodies behind the recursion-guard wrappers below */
static void eval_inner(Exec *e, Node *n, Value *out);
static Value exec_stmt_inner(Exec *e, Node *n);

/* ================= builtins ================= */

static void bi_out_println(Exec *e, Value *a, int n, Value *out, int tag) {
  (void)e; (void)tag;
  for (int i = 0; i < n; i++) { if (i) fputc(' ', stdout); val_print(stdout, a[i]); }
  fputc('\n', stdout);
  out->k = V_VOID;
}

static void bi_out_print(Exec *e, Value *a, int n, Value *out, int tag) {
  (void)e; (void)tag;
  for (int i = 0; i < n; i++) { if (i) fputc(' ', stdout); val_print(stdout, a[i]); }
  out->k = V_VOID;
}

static void bi_out_error(Exec *e, Value *a, int n, Value *out, int tag) {
  (void)e; (void)tag;
  for (int i = 0; i < n; i++) { if (i) fputc(' ', stderr); val_print(stderr, a[i]); }
  out->k = V_VOID;
}

/* The `X.from(y)` cast family (README `from`): never consumes or mutates
 * the argument, always fallible.  tag = TY_* of the target type. */
static void bi_from(Exec *e, Value *a, int n, Value *out, int tag) {
  if (n != 1) vfail(e, NULL, "`from` takes exactly one argument");
  Value x = a[0];
  (void)e;
  switch (tag) {
  case TY_STRING: {          /* string.from(x) — textual */
    char buf[64];
    switch (x.k) {
    case V_INT: snprintf(buf, sizeof buf, "%lld", (long long)x.i); break;
    case V_UINT: snprintf(buf, sizeof buf, "%llu", (unsigned long long)x.u); break;
    case V_FLOAT: snprintf(buf, sizeof buf, "%g", x.f); break;
    case V_BOOL: strcpy(buf, x.b ? "true" : "false"); break;
    case V_CHAR: buf[0] = (char)(x.c32 < 0x80 ? x.c32 : '?'); buf[1] = '\0'; break;
    case V_BYTE: snprintf(buf, sizeof buf, "%u", (unsigned)x.by); break;
    case V_STR: *out = x; return;
    default:
      *out = err_value(e, "cast", "string.from: unsupported value");
      return;
    }
    size_t len = strlen(buf);
    const char *p = savestr(e, buf, len);
    *out = str_value(p, len);
    return;
  }
  case TY_INT: case TY_UINT: {
    int64_t iv;
    switch (x.k) {
    case V_INT: iv = x.i; break;
    case V_UINT:
      if (tag == TY_UINT) { out->k = V_UINT; out->u = x.u; return; }
      if (x.u > (uint64_t)INT64_MAX) goto intfail;
      iv = (int64_t)x.u;
      break;
    case V_FLOAT:
      if (tag == TY_UINT) {
        /* cast well-defined only while the value fits [0, 2^64) */
        if (x.f < 0 || x.f >= 18446744073709551616.0) goto intfail;
        out->k = V_UINT;
        out->u = (uint64_t)x.f;
        return;
      }
      if (isnan(x.f) || x.f >= 9223372036854775808.0 ||
          x.f < -9223372036854775808.0)
        goto intfail;
      iv = (int64_t)x.f;
      break;
    case V_BOOL: iv = x.b; break;
    case V_CHAR: iv = x.c32; break;
    case V_BYTE: iv = x.by; break;
    case V_STR: {
      /* parse the text by hand (values aren't tokens) */
      const char *s = x.str.s;
      char *p = xalloc(e, x.str.n + 1);
      size_t o = 0;
      for (size_t i = 0; i < x.str.n; i++) if (s[i] != '_') p[o++] = s[i];
      p[o] = '\0';
      if (o == 0) goto intfail;
      errno = 0;
      char *end;
      if (p[0] == '-') {
        long long v2 = strtoll(p, &end, 10);
        if (errno || end == p || *end != '\0') goto intfail;
        iv = v2;
      } else {
        unsigned long long u2 = strtoull(p, &end, 10);
        if (errno || end == p || *end != '\0') goto intfail;
        if (u2 > (uint64_t)INT64_MAX) goto intfail;
        iv = (int64_t)u2;
      }
      break;
    }
    default: goto intfail;
    }
    if (tag == TY_INT) { out->k = V_INT; out->i = iv; return; }
    if (iv < 0) goto intfail;
    out->k = V_UINT; out->u = (uint64_t)iv; return;
  intfail:
    *out = err_value(e, "cast", "value is not a valid integer");
    return;
  }
  case TY_FLOAT: {
    switch (x.k) {
    case V_INT: out->k = V_FLOAT; out->f = (double)x.i; return;
    case V_UINT: out->k = V_FLOAT; out->f = (double)x.u; return;
    case V_FLOAT: *out = x; return;
    case V_BOOL: out->k = V_FLOAT; out->f = x.b; return;
    case V_STR: {
      char *p = xalloc(e, x.str.n + 1);
      memcpy(p, x.str.s, x.str.n); p[x.str.n] = '\0';
      char *end;
      double f = strtod(p, &end);
      if (end == p || (size_t)(end - p) != x.str.n || isnan(f))
        goto floatfail;
      out->k = V_FLOAT; out->f = f; return;
    }
    default:
    floatfail:
      *out = err_value(e, "cast", "value is not a valid float");
      return;
    }
  }
  case TY_BOOL: {
    switch (x.k) {
    case V_BOOL: *out = x; return;
    case V_INT: out->k = V_BOOL; out->b = x.i != 0; return;
    case V_UINT: out->k = V_BOOL; out->b = x.u != 0; return;
    case V_FLOAT: out->k = V_BOOL; out->b = x.f != 0.0; return;
    case V_STR:
      if (x.str.n == 4 && memcmp(x.str.s, "true", 4) == 0)
        { out->k = V_BOOL; out->b = 1; return; }
      if (x.str.n == 5 && memcmp(x.str.s, "false", 5) == 0)
        { out->k = V_BOOL; out->b = 0; return; }
      break;
    default: break;
    }
    *out = err_value(e, "cast", "value is not a bool");
    return;
  }
  case TY_CHAR: {
    switch (x.k) {
    case V_CHAR: *out = x; return;
    case V_INT: out->k = V_CHAR; out->c32 = (uint32_t)x.i; return;
    case V_BYTE: out->k = V_CHAR; out->c32 = x.by; return;
    case V_STR:
      if (x.str.n == 1) { out->k = V_CHAR; out->c32 = (unsigned char)x.str.s[0]; return; }
      break;
    default: break;
    }
    *out = err_value(e, "cast", "value is not a char");
    return;
  }
  case TY_BYTE: {
    switch (x.k) {
    case V_BYTE: *out = x; return;
    case V_INT: if (x.i < 0 || x.i > 255) break;
      out->k = V_BYTE; out->by = (uint8_t)x.i; return;
    case V_CHAR: if (x.c32 > 255) break;
      out->k = V_BYTE; out->by = (uint8_t)x.c32; return;
    default: break;
    }
    *out = err_value(e, "cast", "value is not a byte");
    return;
  }
  case TY_BYTES:
    *out = err_value(e, "cast", "bytes.encode is not executable in the bootstrap phase");
    return;
  default:
    *out = err_value(e, "cast", "unsupported `from` target");
    return;
  }
}

/* ================= struct / array construction ================= */

static Value rec_build(Exec *e, const char *tyname, Node *init) {
  const struct Styp *sd = find_struct(e, tyname);
  if (!sd || !sd->fields) vfail(e, init, "unknown struct type `%s`", tyname);
  Node *fields = sd->fields;             /* N_LIST of N_FIELD */
  size_t nf = fields ? fields->n : 0;
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_REC;
  v.rec.tyname = tyname;
  v.rec.n = nf;
  v.rec.fs = xalloc(e, (nf ? nf : 1) * sizeof *v.rec.fs);
  for (size_t i = 0; i < nf; i++) {
    Node *fld = fields->ch[i];
    v.rec.fs[i].nm = savestr(e, ttext(fld->ch[0]), fld->ch[0]->tok.len);
    v.rec.fs[i].v = zero_value(e, fld->ch[1]);
  }
  if (!init || init->k != N_INIT || init->n == 0) return v;
  size_t pos = 0;
  for (int i = 0; i < init->n; i++) {
    Node *item = init->ch[i];            /* N_INITITEM */
    if (item->n >= 2 && item->ch[0] && item->ch[0]->k == N_NAME) {
      const char *fname = savestr(e, ttext(item->ch[0]), item->ch[0]->tok.len);
      size_t f = v.rec.n;
      for (size_t j = 0; j < v.rec.n; j++)
        if (strcmp(v.rec.fs[j].nm, fname) == 0) { f = j; break; }
      if (f == v.rec.n) vfail(e, item, "`%s` has no field `%s`", tyname, fname);
      eval(e, item->ch[1], &v.rec.fs[f].v);
    } else if (item->n >= 1) {
      if (pos >= v.rec.n) vfail(e, item, "too many initializer elements for `%s`", tyname);
      eval(e, item->ch[item->n - 1], &v.rec.fs[pos].v);
      pos++;
    }
  }
  return v;
}

static Value arr_build(Exec *e, Node *init) {
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_ARR;
  v.arr.n = 0;
  v.arr.cap = (size_t)(init && init->k == N_INIT ? init->n : 0);
  v.arr.els = xalloc(e, (v.arr.cap ? v.arr.cap : 1) * sizeof *v.arr.els);
  if (!init || init->k != N_INIT) return v;
  for (int i = 0; i < init->n; i++) {
    Node *item = init->ch[i];
    if (item->n < 1) continue;
    eval(e, item->ch[item->n - 1], &v.arr.els[i]);   /* positional */
    v.arr.n++;
  }
  return v;
}

/* Generic initializer (define-form `x := { … }`): named items make a
 * record, positional items make an array (exactly as the parser shapes
 * them — N_INITITEM ch[0] name? / ch[1] expr). */
static Value init_value(Exec *e, Node *init) {
  for (int i = 0; i < init->n; i++) {
    Node *item = init->ch[i];
    if (item->ch[0] && item->ch[0]->k == N_NAME && item->n >= 2) {
      Value v;
      memset(&v, 0, sizeof v);
      v.k = V_REC;
      v.rec.n = (size_t)init->n;
      v.rec.fs = xalloc(e, v.rec.n * sizeof *v.rec.fs);
      for (int j = 0; j < init->n; j++) {
        Node *it = init->ch[j];
        v.rec.fs[j].nm = savestr(e, ttext(it->ch[0]), it->ch[0]->tok.len);
        eval(e, it->ch[1], &v.rec.fs[j].v);
      }
      return v;
    }
  }
  return arr_build(e, init);
}

/* ================= evaluation ================= */

static void check_bin_arith(Exec *e, const Node *at, Value a, Value b) {
  if (a.k == V_INT && b.k == V_INT) return;
  if (a.k == V_UINT && b.k == V_UINT) return;
  if ((a.k == V_FLOAT || b.k == V_FLOAT) &&
      (a.k == V_INT || a.k == V_UINT || a.k == V_FLOAT) &&
      (b.k == V_INT || b.k == V_UINT || b.k == V_FLOAT)) return;
  if ((a.k == V_INT || a.k == V_UINT) && (b.k == V_INT || b.k == V_UINT)) return;
  vfail(e, at, "arithmetic between %s and %s",
        kind_name(a.k), kind_name(b.k));
}

static Value bin_bitsh(Exec *e, const Node *at, Value a, Value b, TokKind k) {
  check_bin_arith(e, at, a, b);
  int64_t x = a.k == V_UINT ? (int64_t)a.u : a.i;
  int64_t sh = b.k == V_UINT ? (int64_t)b.u : b.i;
  int64_t r;
  if (sh < 0 || sh > 63) vfail(e, at, "shift by an out-of-range amount");
  switch (k) {
  case T_OP_SHL: r = (int64_t)((uint64_t)x << sh); break;
  case T_OP_SHR: r = x >> sh; break;
  case T_OP_USHR: r = (int64_t)((uint64_t)x >> sh); break;
  case T_OP_SHL_CY:
    r = x == 0 ? 0 : (int64_t)((uint64_t)x << sh) |
        (sh == 0 ? 0 : (x < 0 ? (int64_t)((uint64_t)x >> (uint64_t)(64 - sh)) : 0));
    break;
  case T_OP_SHR_CY:
    r = x == 0 ? 0 : x < 0
        ? (int64_t)((uint64_t)x >> sh) : (int64_t)((uint64_t)x >> sh);
    break;
  default: r = 0; break;
  }
  Value v;
  memset(&v, 0, sizeof v);
  if (a.k == V_UINT || b.k == V_UINT) { v.k = V_UINT; v.u = (uint64_t)r; }
  else { v.k = V_INT; v.i = r; }
  return v;
}

static Value bin_bit(Exec *e, const Node *at, Value a, Value b, TokKind k) {
  check_bin_arith(e, at, a, b);
  uint64_t x = a.k == V_UINT ? a.u : (uint64_t)a.i;
  uint64_t y = b.k == V_UINT ? b.u : (uint64_t)b.i;
  uint64_t r = k == T_OP_PIPE ? (x | y) : k == T_OP_CARET ? (x ^ y) : (x & y);
  Value v;
  memset(&v, 0, sizeof v);
  if (a.k == V_UINT || b.k == V_UINT) { v.k = V_UINT; v.u = r; }
  else { v.k = V_INT; v.i = (int64_t)r; }
  return v;
}

/* The descending-range `>` `..=` / `>` `..<` pair is folded by the parser
 * into a single N_BIN whose tok is the `..=` / `..<` token; the pair must
 * be literally adjacent, so the character right before that token in the
 * source is `>` exactly when the range descends. */
static int range_desc(Exec *e, const Node *n) {
  return n->tok.start > e->src && n->tok.start[-1] == '>';
}

static Value num_add(Exec *e, const Node *at, Value a, Value b, int op) {
  check_bin_arith(e, at, a, b);
  if (a.k == V_FLOAT || b.k == V_FLOAT) {
    double x = a.k == V_FLOAT ? a.f : a.k == V_INT ? (double)a.i : (double)a.u;
    double y = b.k == V_FLOAT ? b.f : b.k == V_INT ? (double)b.i : (double)b.u;
    Value v;
    memset(&v, 0, sizeof v);
    v.k = V_FLOAT;
    switch (op) {
    case 0: v.f = x + y; break;
    case 1: v.f = x - y; break;
    case 2: v.f = x * y; break;
    case 3: if (y == 0) vfail(e, at, "division by zero");
            v.f = x / y; break;
    case 4: v.f = fmod(x, y); break;
    default: vfail(e, at, "bad float operator");
    }
    return v;
  }
  int64_t x = a.k == V_UINT ? (int64_t)(uint64_t)a.u : a.i;
  int64_t y = b.k == V_UINT ? (int64_t)(uint64_t)b.u : b.i;
  Value v;
  memset(&v, 0, sizeof v);
  v.k = (a.k == V_UINT || b.k == V_UINT) ? V_UINT : V_INT;
  switch (op) {
  case 0: {
    uint64_t r = (uint64_t)x + (uint64_t)y;
    if (v.k == V_UINT) v.u = r; else v.i = (int64_t)r;
    return v;
  }
  case 1: {
    uint64_t r = (uint64_t)x - (uint64_t)y;
    if (v.k == V_UINT) v.u = r; else v.i = (int64_t)r;
    return v;
  }
  case 2: {
    uint64_t r = (uint64_t)x * (uint64_t)y;
    if (v.k == V_UINT) v.u = r; else v.i = (int64_t)r;
    return v;
  }
  case 3:
    if (y == 0) vfail(e, at, "division by zero");
    if (v.k == V_INT && x == INT64_MIN && y == -1)
      vfail(e, at, "integer division overflow");
    if (v.k == V_UINT) v.u = (uint64_t)x / (uint64_t)y;
    else v.i = x / y;
    return v;
  case 4:
    if (y == 0) vfail(e, at, "division by zero");
    if (v.k == V_INT && x == INT64_MIN && y == -1)
      vfail(e, at, "integer modulo overflow");
    if (v.k == V_UINT) v.u = (uint64_t)x % (uint64_t)y;
    else v.i = x % y;
    return v;
  default:
    vfail(e, at, "bad integer operator");
  }
}

static Value num_cmp(Exec *e, const Node *at, Value a, Value b, int op) {
  /* op: 0 == 1 != 2 < 3 <= 4 > 5 >= */
  int r;
  if (a.k == V_STR && b.k == V_STR) {
    size_t m = a.str.n < b.str.n ? a.str.n : b.str.n;
    int c = memcmp(a.str.s, b.str.s, m);
    if (c == 0) c = (a.str.n > b.str.n) - (a.str.n < b.str.n);
    r = c;
  } else if (a.k == V_CHAR && b.k == V_CHAR) {
    r = (a.c32 > b.c32) - (a.c32 < b.c32);
  } else if ((a.k == V_INT || a.k == V_UINT) &&
             (b.k == V_INT || b.k == V_UINT)) {
    uint64_t x = a.k == V_UINT ? a.u : (uint64_t)(int64_t)a.i;
    uint64_t y = b.k == V_UINT ? b.u : (uint64_t)(int64_t)b.i;
    int xs = a.k == V_INT && a.i < 0, ys = b.k == V_INT && b.i < 0;
    if (xs != ys) r = xs ? -1 : 1;
    else r = (x > y) - (x < y);
  } else if (a.k == V_FLOAT || b.k == V_FLOAT) {
    double x = a.k == V_FLOAT ? a.f : a.k == V_INT ? (double)a.i : (double)a.u;
    double y = b.k == V_FLOAT ? b.f : b.k == V_INT ? (double)b.i : (double)b.u;
    r = (x > y) - (x < y);
  } else {
    vfail(e, at, "cannot compare %s with %s", kind_name(a.k), kind_name(b.k));
  }
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_BOOL;
  switch (op) {
  case 0: v.b = r == 0; break;
  case 1: v.b = r != 0; break;
  case 2: v.b = r < 0; break;
  case 3: v.b = r <= 0; break;
  case 4: v.b = r > 0; break;
  case 5: v.b = r >= 0; break;
  }
  return v;
}

/* Type value for a fundamental keyword token. */
static Value type_value(Exec *e, const Node *n) {
  Value v;
  memset(&v, 0, sizeof v);
  (void)e;
  v.k = V_TYPE;
  const char *t = savestr(e, ttext(n), n->tok.len);
  v.ty = strcmp(t, "byte") == 0 ? TY_BYTE :
         strcmp(t, "char") == 0 ? TY_CHAR :
         strcmp(t, "int") == 0 ? TY_INT :
         strcmp(t, "uint") == 0 ? TY_UINT :
         strcmp(t, "float") == 0 ? TY_FLOAT :
         strcmp(t, "bool") == 0 ? TY_BOOL :
         strcmp(t, "string") == 0 ? TY_STRING :
         strcmp(t, "bytes") == 0 ? TY_BYTES : TY_ERROR;
  return v;
}

static Value func_value(Exec *e, Func f);

/* Recursion guard: every crossing of eval/exec_stmt bumps `depth`; past
 * `depth_max` the interpreter dies with a clean diagnostic instead of
 * overflowing the C stack.  A lang call alternates both, so `depth_max`
 * counts crossings, not language recursion levels. */
static void eval(Exec *e, Node *n, Value *out) {
  if (++e->depth > e->depth_max) vfail(e, n, "recursion limit exceeded");
  eval_inner(e, n, out);
  e->depth--;
}

static void eval_inner(Exec *e, Node *n, Value *out) {
  if (e->flow != FL_NONE) { out->k = V_VOID; return; }  /* flow shields */
  Value v;
  memset(&v, 0, sizeof v);
  switch (n->k) {
  case N_LIT:
    *out = lit_value(e, n);
    return;
  case N_NAME: {
    /* fundamental-type keyword primaries evaluate to type values, whose
     * `.from` member is the cast family */
    switch (n->tok.kind) {
    case T_KW_BYTE: case T_KW_CHAR: case T_KW_INT: case T_KW_UINT:
    case T_KW_FLOAT: case T_KW_BOOL: case T_KW_STRING: case T_KW_BYTES:
    case T_KW_ERROR:
      *out = type_value(e, n);
      return;
    default: break;
    }
    const char *nm = savestr(e, ttext(n), n->tok.len);
    if (!env_lookup(e->cur, nm, &v))
      vfail(e, n, "unknown name `%s`", nm);
    *out = v;
    return;
  }
  case N_TYPEARGS: case N_TYPEARG:       /* stray type-arg refs */
    vfail(e, n, "type arguments are not executable in the bootstrap phase");
    return;
  default:
    break;
  }
  switch (n->k) {
  case N_BIN: {
    Value a, b;
    eval(e, n->ch[0], &a);
    eval(e, n->ch[1], &b);
    TokKind k = n->tok.kind;
    if (k == T_OP_ADD_ASSIGN || k == T_OP_SUB_ASSIGN || k == T_OP_MUL_ASSIGN ||
        k == T_OP_DIV_ASSIGN || k == T_OP_MOD_ASSIGN) {
      /* a compound-assign spelled as a bin (never produced by the parser;
       * compound assigns are N_ASSIGN). Defensive. */
      int opi = k == T_OP_ADD_ASSIGN ? 0 : k == T_OP_SUB_ASSIGN ? 1 :
                k == T_OP_MUL_ASSIGN ? 2 : k == T_OP_DIV_ASSIGN ? 3 : 4;
      *out = num_add(e, n, a, b, opi);
      return;
    }
    switch (k) {
    case T_OP_PLUS: *out = num_add(e, n, a, b, 0); return;
    case T_OP_MINUS: *out = num_add(e, n, a, b, 1); return;
    case T_OP_STAR: *out = num_add(e, n, a, b, 2); return;
    case T_OP_SLASH: *out = num_add(e, n, a, b, 3); return;
    case T_OP_PERCENT: *out = num_add(e, n, a, b, 4); return;
    case T_OP_SHL: case T_OP_SHR: case T_OP_USHR:
    case T_OP_SHL_CY: case T_OP_SHR_CY:
      *out = bin_bitsh(e, n, a, b, k);
      return;
    case T_OP_AMP: case T_OP_PIPE: case T_OP_CARET:
      *out = bin_bit(e, n, a, b, k);
      return;
    case T_OP_AND: case T_OP_OR: {
      int x = val_truth(e, n, a);
      if (k == T_OP_AND && !x) { v.k = V_BOOL; v.b = 0; *out = v; return; }
      if (k == T_OP_OR && x) { v.k = V_BOOL; v.b = 1; *out = v; return; }
      int y = val_truth(e, n, b);
      v.k = V_BOOL; v.b = y;
      *out = v;
      return;
    }
    case T_OP_QQ:
      *out = a.k == V_ERR ? b : a;
      return;
    case T_OP_EQ: *out = num_cmp(e, n, a, b, 0); return;
    case T_OP_NE: *out = num_cmp(e, n, a, b, 1); return;
    case T_OP_LT: *out = num_cmp(e, n, a, b, 2); return;
    case T_OP_LE: *out = num_cmp(e, n, a, b, 3); return;
    case T_OP_GT: *out = num_cmp(e, n, a, b, 4); return;
    case T_OP_GE: *out = num_cmp(e, n, a, b, 5); return;
    case T_OP_RANGE_LE: case T_OP_RANGE_LT: {
      if (a.k != V_INT || b.k != V_INT)
        vfail(e, n, "ranges need int bounds");
      v.k = V_RANGE;
      v.rng.lo = a.i;
      v.rng.hi = b.i;
      v.rng.desc = range_desc(e, n);
      v.rng.incl = k == T_OP_RANGE_LE;
      *out = v;
      return;
    }
    default: vfail(e, n, "`%s` is not an executable binary operator",
                   token_kind_name(k));
    }
    return;
  }
  case N_UNARY: {
    const char *op = ttext(n);
    eval(e, n->ch[0], &v);
    switch (n->tok.kind) {
    case T_OP_MINUS:
      if (v.k == V_INT) { v.i = -v.i; *out = v; return; }
      if (v.k == V_UINT) { v.k = V_INT; v.i = -(int64_t)v.u; *out = v; return; }
      if (v.k == V_FLOAT) { v.f = -v.f; *out = v; return; }
      vfail(e, n, "cannot negate a %s", kind_name(v.k));
    case T_BANG: v.k = V_BOOL; v.b = !val_truth(e, n, v); *out = v; return;
    case T_OP_TILDE:
      if (v.k == V_INT) { v.i = ~v.i; *out = v; return; }
      if (v.k == V_UINT) { v.u = ~v.u; *out = v; return; }
      vfail(e, n, "cannot bitwise-not a %s", kind_name(v.k));
    default: vfail(e, n, "unary `%s` is not executable in the bootstrap phase", op);
    }
  }
  case N_MEMBER: {
    Value base;
    eval(e, n->ch[0], &base);
    const char *m = savestr(e, ttext(n->ch[1]), n->ch[1]->tok.len);
    switch (base.k) {
    case V_REC: {
      for (size_t i = 0; i < base.rec.n; i++)
        if (strcmp(base.rec.fs[i].nm, m) == 0) { *out = base.rec.fs[i].v; return; }
      vfail(e, n, "record has no field `%s`", m);
    }
    case V_STR:
      if (strcmp(m, "length") == 0) {
        v.k = V_INT; v.i = (int64_t)base.str.n; *out = v; return;
      }
      vfail(e, n, "string has no member `%s`", m);
    case V_ARR:
      if (strcmp(m, "length") == 0) {
        v.k = V_INT; v.i = (int64_t)base.arr.n; *out = v; return;
      }
      vfail(e, n, "array has no member `%s`", m);
    case V_ERR:
      if (strcmp(m, "message") == 0) {
        *out = str_value(base.err.msg.s, base.err.msg.n);
        return;
      }
      vfail(e, n, "error has no member `%s`", m);
    case V_TYPE:
      if (strcmp(m, "from") == 0) {
        Func f;
        memset(&f, 0, sizeof f);
        f.kind = F_BUILTIN;
        f.tag = base.ty;
        f.name = "from";
        f.fn = bi_from;
        *out = func_value(e, f);
        return;
      }
      vfail(e, n, "type value has no member `%s`", m);
    default:
      vfail(e, n, "a %s value has no member `%s`", kind_name(base.k), m);
    }
  }
  case N_CALL: {
    Value cal;
    eval(e, n->ch[0], &cal);
    Node *al = n->n >= 2 ? n->ch[n->n - 1] : NULL;   /* args = last child */
    Value *av = NULL;
    int na = 0;
    if (al && al->k == N_LIST) {
      if (al->n > 0) {
        av = xalloc(e, (size_t)al->n * sizeof *av);
        for (int i = 0; i < al->n; i++) {
          Node *a = al->ch[i];
          if (a->k == N_INITITEM) {
            /* named argument `f(x = …)`: evaluate the value part */
            eval(e, a->ch[1], &av[i]);
          } else {
            eval(e, a, &av[i]);
          }
        }
        na = al->n;
      }
    }
    if (cal.k == V_FUNC) {
      if (cal.fn.f.kind == F_BUILTIN)
        cal.fn.f.fn(e, av, na, out, cal.fn.f.tag);
      else
        *out = invoke_user(e, cal.fn.f, av, na, n);
      return;
    }
    vfail(e, n, "trying to call a %s value", kind_name(cal.k));
  }
  case N_INDEX: {
    Value base, ix;
    eval(e, n->ch[0], &base);
    eval(e, n->ch[1], &ix);
    if (ix.k != V_INT) vfail(e, n, "index must be an int");
    int64_t i = ix.i;
    if (base.k == V_ARR) {
      if (i < 0 || (size_t)i >= base.arr.n) vfail(e, n, "array index out of bounds");
      *out = base.arr.els[i];
      return;
    }
    if (base.k == V_STR) {
      if (i < 0 || (size_t)i >= base.str.n) vfail(e, n, "string index out of bounds");
      v.k = V_CHAR; v.c32 = (unsigned char)base.str.s[i]; *out = v;
      return;
    }
    vfail(e, n, "cannot index a %s", kind_name(base.k));
  }
  case N_SLICE: {
    Value base, lo = {0}, hi = {0};
    eval(e, n->ch[0], &base);
    int64_t l = 0, h;
    if (n->n >= 2 && n->ch[1]) { eval(e, n->ch[1], &lo); if (lo.k != V_INT) vfail(e, n, "slice bound must be an int"); l = lo.i; }
    if (base.k == V_ARR) h = (int64_t)base.arr.n; else h = (int64_t)base.str.n;
    if (n->n >= 3 && n->ch[2]) { eval(e, n->ch[2], &hi); if (hi.k != V_INT) vfail(e, n, "slice bound must be an int"); h = hi.i; }
    l = l < 0 ? 0 : l; h = h > (int64_t)(base.k == V_ARR ? base.arr.n : base.str.n) ? (int64_t)(base.k == V_ARR ? base.arr.n : base.str.n) : h;
    Value outv;
    memset(&outv, 0, sizeof outv);
    if (base.k == V_STR) {
      if (l > h || h < 0) vfail(e, n, "bad string slice bounds");
      outv.k = V_STR; outv.str.s = base.str.s + l; outv.str.n = (size_t)(h - l);
      *out = outv;
      return;
    }
    if (base.k == V_ARR) {
      if (l > h || h < 0) vfail(e, n, "bad array slice bounds");
      size_t n2 = (size_t)(h - l);
      outv.k = V_ARR;
      outv.arr.n = n2;
      outv.arr.cap = n2;
      outv.arr.els = xalloc(e, (n2 ? n2 : 1) * sizeof *outv.arr.els);
      for (size_t j = 0; j < n2; j++) outv.arr.els[j] = base.arr.els[l + j];
      *out = outv;
      return;
    }
    vfail(e, n, "cannot slice a %s", kind_name(base.k));
  }
  case N_UNWRAP: {
    eval(e, n->ch[0], &v);
    if (n->tok.kind == T_OP_QMARK) unwrap_soft(e, &v);
    else unwrap_hard(e, n, &v);
    *out = v;
    return;
  }
  case N_INIT:
    *out = init_value(e, n);
    return;
  case N_STRUCT_LIT: {
    const char *nm = savestr(e, ttext(n->ch[0]), n->ch[0]->tok.len);
    Node *init = n->n >= 2 ? n->ch[n->n - 1] : NULL;   /* init = last child */
    *out = rec_build(e, nm, init);
    return;
  }
  case N_FUNC_EXPR: {
    Func f;
    memset(&f, 0, sizeof f);
    f.kind = F_USER;
    f.name = "(anon)";
    f.params = n->ch[0];
    f.body = n->ch[2];
    f.closure = e->cur;
    *out = func_value(e, f);
    return;
  }
  case N_IS: {
    Value x;
    eval(e, n->ch[0], &x);
    const char *kind = savestr(e, ttext(n->ch[1]), n->ch[1]->tok.len);
    int yes = x.k == V_ERR && strcmp(x.err.name, kind) == 0;
    if (n->n >= 3 && n->ch[2] && yes) {
      env_bind(e, e->cur,
               savestr(e, ttext(n->ch[2]), n->ch[2]->tok.len), x);
    }
    v.k = V_BOOL; v.b = yes;
    *out = v;
    return;
  }
  case N_TRAILING:
    vfail(e, n, "trailing-block calls are not executable in the bootstrap phase");
    return;
  case N_ANON_STRUCT:
    vfail(e, n, "anonymous struct literals are not executable in the bootstrap phase");
    return;
  case N_RECV:
    vfail(e, n, "channel receives are not executable in the bootstrap phase");
    return;
  default:
    vfail(e, n, "`%s` is not an executable expression", node_kind_name(n->k));
  }
}

static Value func_value(Exec *e, Func f) {
  Value v;
  memset(&v, 0, sizeof v);
  (void)e;
  v.k = V_FUNC;
  v.fn.nm = f.name ? f.name : "(anon)";
  v.fn.f = f;
  return v;
}

/* ================= statement execution ================= */

static Value void_value(void) {
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_VOID;
  return v;
}

static Value int_value(int64_t i) {
  Value v;
  memset(&v, 0, sizeof v);
  v.k = V_INT;
  v.i = i;
  return v;
}

/* Resolve an lvalue to the cell holding its current value. */
static Value *lvalue_cell(Exec *e, Node *lv) {
  switch (lv->k) {
  case N_NAME: {
    const char *nm = savestr(e, ttext(lv), lv->tok.len);
    for (Env *f = e->cur; f; f = f->parent)
      for (size_t i = f->n; i-- > 0;)
        if (strcmp(f->b[i].nm, nm) == 0) return &f->b[i].v;
    vfail(e, lv, "unknown name `%s`", nm);
  }
  case N_MEMBER: {
    Value base;
    eval(e, lv->ch[0], &base);
    if (base.k != V_REC)
      vfail(e, lv, "cannot assign to a member of a %s value", kind_name(base.k));
    const char *m = savestr(e, ttext(lv->ch[1]), lv->ch[1]->tok.len);
    for (size_t i = 0; i < base.rec.n; i++)
      if (strcmp(base.rec.fs[i].nm, m) == 0) return &base.rec.fs[i].v;
    vfail(e, lv, "record has no field `%s`", m);
  }
  case N_INDEX: {
    Value base, ix;
    eval(e, lv->ch[0], &base);
    eval(e, lv->ch[1], &ix);
    if (base.k != V_ARR)
      vfail(e, lv, "cannot index-assign a %s value", kind_name(base.k));
    if (ix.k != V_INT) vfail(e, lv, "index must be an int");
    int64_t i = ix.i;
    if (i < 0 || (size_t)i >= base.arr.n)
      vfail(e, lv, "array index out of bounds");
    return &base.arr.els[i];
  }
  default:
    vfail(e, lv, "`%s` is not an assignable lvalue", node_kind_name(lv->k));
  }
}

/* Plain assignment and the compound-assignment family (C23). */
static void assign_op(Exec *e, const Node *n, Value *cell, Value rv) {
  switch (n->tok.kind) {
  case T_OP_ASSIGN: case T_OP_DEFINE: *cell = rv; return;
  case T_OP_ADD_ASSIGN: *cell = num_add(e, n, *cell, rv, 0); return;
  case T_OP_SUB_ASSIGN: *cell = num_add(e, n, *cell, rv, 1); return;
  case T_OP_MUL_ASSIGN: *cell = num_add(e, n, *cell, rv, 2); return;
  case T_OP_DIV_ASSIGN: *cell = num_add(e, n, *cell, rv, 3); return;
  case T_OP_MOD_ASSIGN: *cell = num_add(e, n, *cell, rv, 4); return;
  case T_OP_SHL_ASSIGN: *cell = bin_bitsh(e, n, *cell, rv, T_OP_SHL); return;
  case T_OP_SHR_ASSIGN: *cell = bin_bitsh(e, n, *cell, rv, T_OP_SHR); return;
  case T_OP_USHR_ASSIGN: *cell = bin_bitsh(e, n, *cell, rv, T_OP_USHR); return;
  case T_OP_AND_ASSIGN: *cell = bin_bit(e, n, *cell, rv, T_OP_AMP); return;
  case T_OP_OR_ASSIGN: *cell = bin_bit(e, n, *cell, rv, T_OP_PIPE); return;
  case T_OP_XOR_ASSIGN: *cell = bin_bit(e, n, *cell, rv, T_OP_CARET); return;
  default:
    vfail(e, n, "`%s` is not an executable assignment op",
          token_kind_name(n->tok.kind));
  }
}

/* ---------------- defers ---------------- */

static void defer_append(Exec *e, Node *n) {
  if (e->nd == e->capd) {
    size_t nc = e->capd ? e->capd * 2 : 16;
    Node **nd2 = xalloc(e, nc * sizeof *nd2);
    if (e->defers) memcpy(nd2, e->defers, e->nd * sizeof *nd2);
    e->defers = nd2;
    e->capd = nc;
  }
  e->defers[e->nd++] = n;
}

static void defer_run_from(Exec *e, size_t from) {
  for (size_t i = from; i < e->nd; i++) {
    if (e->flow != FL_NONE) break;
    exec_stmt(e, e->defers[i]->ch[0]);
  }
}

/* ---------------- loops & patterns ---------------- */

/* One translation-unit iteration of an in-clause: bind the loop variables,
 * run the body, consume an unlabeled break/continue.  Returns whether the
 * loop should keep going. */
static int in_iterate(Exec *e, Node *body, int has_ord, const char *ord_name,
                      int has_elem, const char *elem_name,
                      Value ord_val, Value elem_val) {
  if (e->flow != FL_NONE) return 0;
  if (has_ord) env_bind(e, e->cur, ord_name, ord_val);
  if (has_elem && !(elem_name[0] == '_' && elem_name[1] == '\0'))
    env_bind(e, e->cur, elem_name, elem_val);
  if (body && body->k == N_BLOCK) exec_block(e, body);
  else if (body) exec_stmt(e, body);
  if (e->flow == FL_CONTINUE && e->label == NULL) { e->flow = FL_NONE; return 1; }
  if (e->flow == FL_BREAK && e->label == NULL) { e->flow = FL_NONE; return 0; }
  return e->flow == FL_NONE;
}

/* Does `subj` match one pattern node?  Constructor-pattern payloads are not
 * bound in the bootstrap phase. */
static int match_one(Exec *e, Node *pat, Value subj) {
  switch (pat->k) {
  case N_NAME: {
    if (pat->tok.len == 1 && pat->tok.start[0] == '_') return 1;
    const char *nm = savestr(e, ttext(pat), pat->tok.len);
    Value pv;
    if (!env_lookup(e->cur, nm, &pv)) return 0;
    if (subj.k == V_ENUM && pv.k == V_ENUM)
      return subj.en.tag == pv.en.tag && strcmp(subj.en.name, pv.en.name) == 0;
    return val_eq(e, pat, subj, pv);
  }
  case N_LIT: {
    Value lv = lit_value(e, pat);
    return val_eq(e, pat, subj, lv);
  }
  case N_CALL: {
    if (subj.k != V_ENUM) return 0;
    const char *cname = ttext(pat->ch[0]);
    size_t cl = pat->ch[0]->tok.len;
    return strlen(subj.en.name) == cl &&
           memcmp(subj.en.name, cname, cl) == 0;
  }
  default: {
    if (e->flow != FL_NONE) return 0;
    Value g;
    eval(e, pat, &g);
    return val_truth(e, pat, g);
  }
  }
}

/* A guarded region: `try` statements, then optional `catch name` handlers.
 * The catch frames live in the exec arena so that a longjmp cannot stale
 * them, and all state the handlers need is read from the frame. */
static void exec_region(Exec *e, Node *n) {
  Catch *cf = xalloc(e, sizeof *cf);
  cf->prev = e->catch_;
  cf->saved_cur = e->cur;
  cf->saved_nd = e->nd;
  cf->saved_depth = e->depth;
  cf->regs = n->ch[0];
  cf->catch_name = n->ch[1];
  cf->handlers = n->ch[2];
  e->catch_ = cf;
  if (setjmp(cf->buf) == 0) {
    if (cf->regs)
      for (int i = 0; i < cf->regs->n && e->flow == FL_NONE; i++)
        exec_stmt(e, cf->regs->ch[i]);
    if (e->flow == FL_NONE) {
      defer_run_from(e, cf->saved_nd);
      e->nd = cf->saved_nd;             /* drain the region's defers */
    }
    e->catch_ = cf->prev;
  } else {
    /* a raise landed at this region */
    e->catch_ = cf->prev;
    e->cur = cf->saved_cur;
    e->nd = cf->saved_nd;               /* drop the aborted region's defers */
    e->depth = cf->saved_depth;         /* the unwound frames didn't return */
    e->flow = FL_NONE;
    if (cf->catch_name && cf->catch_name->k == N_NAME) {
      const char *nm = savestr(e, ttext(cf->catch_name), cf->catch_name->tok.len);
      env_bind(e, e->cur, nm, e->exc);
    }
    if (cf->handlers)
      for (int i = 0; i < cf->handlers->n && e->flow == FL_NONE; i++)
        exec_stmt(e, cf->handlers->ch[i]);
  }
}

/* ---------------- statements ---------------- */

static Value exec_stmt(Exec *e, Node *n) {
  if (++e->depth > e->depth_max) vfail(e, n, "recursion limit exceeded");
  Value last = exec_stmt_inner(e, n);
  e->depth--;
  return last;
}

static Value exec_stmt_inner(Exec *e, Node *n) {
  Value last = void_value();
  if (e->flow != FL_NONE) return last;
  switch (n->k) {
  case N_BLOCK:
    return exec_block(e, n);
  case N_LIST:
    for (int i = 0; i < n->n && e->flow == FL_NONE; i++)
      last = exec_stmt(e, n->ch[i]);
    return last;
  case N_EXPR_STMT:
    eval(e, n->ch[0], &last);
    return last;
  case N_VAR: {
    Node *nm = n->ch[0];
    Node *ty = NULL, *init = NULL;
    if (n->tok.kind == T_OP_DEFINE) {
      init = n->n >= 2 ? n->ch[1] : NULL;
    } else {
      if (n->n >= 2) ty = n->ch[1];
      if (n->n >= 3) init = n->ch[2];
    }
    Value val;
    if (n->tok.kind == T_OP_DEFINE) {
      if (!init) vfail(e, n, "a `:=` declaration needs an initializer");
      val = init->k == N_INIT ? init_value(e, init) : void_value();
      if (init->k != N_INIT) eval(e, init, &val);
    } else if (init) {
      if (init->k == N_INIT) {
        const char *tyname = NULL;
        if (ty && ty->k == N_TNAMED && ty->ch[0]) {
          tyname = savestr(e, ttext(ty->ch[0]), ty->ch[0]->tok.len);
          if (find_struct(e, tyname)) val = rec_build(e, tyname, init);
          else { tyname = NULL; val = arr_build(e, init); }
        } else {
          val = arr_build(e, init);
        }
        (void)tyname;
      } else {
        eval(e, init, &val);
      }
    } else {
      val = zero_value(e, ty);
    }
    if (!(nm->tok.len == 1 && nm->tok.start[0] == '_'))
      env_bind(e, e->cur, savestr(e, ttext(nm), nm->tok.len), val);
    return val;
  }
  case N_ASSIGN: {
    Value *cell = lvalue_cell(e, n->ch[0]);
    Value rv;
    eval(e, n->ch[1], &rv);
    assign_op(e, n, cell, rv);
    return last;
  }
  case N_SWAP: {
    Value *ca = lvalue_cell(e, n->ch[0]);
    Value *cb = lvalue_cell(e, n->ch[1]);
    Value t = *ca;
    *ca = *cb;
    *cb = t;
    return last;
  }
  case N_SEND:
    vfail(e, n, "channel sends are not executable in the bootstrap phase");
  case N_IF: {
    Value c;
    eval(e, n->ch[0], &c);
    if (val_truth(e, n, c)) {
      if (n->n >= 2 && n->ch[1]) exec_stmt(e, n->ch[1]);
    } else if (n->n >= 3 && n->ch[2]) {
      exec_stmt(e, n->ch[2]);
    }
    return last;
  }
  case N_CHECK_IF: {
    /* fixed shape: ch[0] name-or-NULL, ch[1] cond, ch[2] then, ch[3] else */
    Value v;
    eval(e, n->ch[1], &v);
    int present = !(v.k == V_ERR || v.k == V_VOID);
    if (present) {
      Node *nm = n->ch[0];
      if (nm && nm->k == N_NAME && !(nm->tok.len == 1 && nm->tok.start[0] == '_'))
        env_bind(e, e->cur, savestr(e, ttext(nm), nm->tok.len), v);
      if (n->ch[2]) exec_stmt(e, n->ch[2]);
    } else if (n->ch[3]) {
      exec_stmt(e, n->ch[3]);
    }
    return last;
  }
  case N_LOOP: {
    /* `loop { … } [until c]` when ch[0] is a block; else `cond do body`. */
    Node *a = n->ch[0], *b = n->ch[1];
    if (a->k == N_BLOCK) {
      for (;;) {
        exec_block(e, a);
        if (e->flow == FL_CONTINUE && e->label == NULL) e->flow = FL_NONE;
        if (e->flow == FL_BREAK && e->label == NULL) { e->flow = FL_NONE; break; }
        if (e->flow != FL_NONE) break;
        if (b) {
          Value u;
          eval(e, b, &u);
          if (val_truth(e, b, u)) break;
        }
      }
    } else {
      for (;;) {
        Value c;
        eval(e, a, &c);
        if (!val_truth(e, a, c)) break;
        exec_stmt(e, b);
        if (e->flow == FL_CONTINUE && e->label == NULL) e->flow = FL_NONE;
        if (e->flow == FL_BREAK && e->label == NULL) { e->flow = FL_NONE; break; }
        if (e->flow != FL_NONE) break;
      }
    }
    return last;
  }
  case N_CFOR: {
    /* ch[0] name ch[1] init ch[2] cond ch[3] post? — body is the last child */
    const char *nm = savestr(e, ttext(n->ch[0]), n->ch[0]->tok.len);
    Node *body = n->ch[n->n - 1];
    Node *post = n->n >= 5 ? n->ch[3] : NULL;
    Value iv;
    eval(e, n->ch[1], &iv);
    if (!(n->ch[0]->tok.len == 1 && n->ch[0]->tok.start[0] == '_'))
      env_bind(e, e->cur, nm, iv);
    for (;;) {
      Value c;
      eval(e, n->ch[2], &c);
      if (!val_truth(e, n->ch[2], c)) break;
      exec_stmt(e, body);
      if (e->flow == FL_CONTINUE && e->label == NULL) e->flow = FL_NONE;
      if (e->flow == FL_BREAK && e->label == NULL) { e->flow = FL_NONE; break; }
      if (e->flow != FL_NONE) break;
      if (post) exec_stmt(e, post);
    }
    return last;
  }
  case N_IN_CLAUSE: {
    /* ch: [name, coll, body] or [ord, name, coll, body] — tail-anchored */
    Node *body = n->ch[n->n - 1];
    Node *coll = n->ch[n->n - 2];
    Node *nm = n->ch[n->n - 3];
    Node *ord = n->n >= 4 ? n->ch[0] : NULL;
    const char *onm = ord ? savestr(e, ttext(ord), ord->tok.len) : NULL;
    const char *enm = savestr(e, ttext(nm), nm->tok.len);
    Value cv;
    eval(e, coll, &cv);
    if (cv.k == V_RANGE) {
      for (int64_t i = cv.rng.lo;
           cv.rng.desc ? (cv.rng.incl ? i >= cv.rng.hi : i > cv.rng.hi)
                       : (cv.rng.incl ? i <= cv.rng.hi : i < cv.rng.hi);
           cv.rng.desc ? i-- : i++) {
        if (!in_iterate(e, body, ord != NULL, onm, 1, enm,
                        int_value(i), int_value(i))) break;
      }
    } else if (cv.k == V_ARR) {
      for (size_t i = 0; i < cv.arr.n; i++) {
        if (!in_iterate(e, body, ord != NULL, onm, 1, enm,
                        int_value((int64_t)i), cv.arr.els[i])) break;
      }
    } else if (cv.k == V_STR) {
      for (size_t i = 0; i < cv.str.n; i++) {
        Value ch = void_value();
        ch.k = V_CHAR;
        ch.c32 = (unsigned char)cv.str.s[i];
        if (!in_iterate(e, body, ord != NULL, onm, 1, enm,
                        int_value((int64_t)i), ch)) break;
      }
    } else {
      vfail(e, n, "in-clause needs an array, string, or range value");
    }
    return last;
  }
  case N_CHECK_IN: {
    /* ch[0] name ch[1] expr ch[2] body */
    const char *nm = savestr(e, ttext(n->ch[0]), n->ch[0]->tok.len);
    for (;;) {
      Value v;
      eval(e, n->ch[1], &v);
      if (v.k == V_ERR || v.k == V_VOID) break;
      if (!(n->ch[0]->tok.len == 1 && n->ch[0]->tok.start[0] == '_'))
        env_bind(e, e->cur, nm, v);
      exec_stmt(e, n->ch[2]);
      if (e->flow == FL_CONTINUE && e->label == NULL) e->flow = FL_NONE;
      if (e->flow == FL_BREAK && e->label == NULL) { e->flow = FL_NONE; break; }
      if (e->flow != FL_NONE) break;
    }
    return last;
  }
  case N_MATCH: {
    Value subj;
    eval(e, n->ch[0], &subj);
    Node *arms = n->ch[1];
    int matched = 0;
    if (arms) {
      for (int i = 0; i < arms->n && !matched; i++) {
        Node *arm = arms->ch[i];            /* N_MATCH_ARM */
        Node *pats = arm->ch[0];
        for (int j = 0; j < pats->n && !matched; j++) {
          if (match_one(e, pats->ch[j], subj)) {
            matched = 1;
            exec_stmt(e, arm->ch[1]);       /* arm body: block or list */
          }
        }
      }
    }
    if (!matched) vfail(e, n, "no match arm matched the subject");
    return last;
  }
  case N_SELECT:
    vfail(e, n, "`select` is not executable in the bootstrap phase (no coroutines)");
  case N_SPAWN:
    vfail(e, n, "`spawn` is not executable in the bootstrap phase (no coroutines)");
  case N_TRY:
    exec_stmt(e, n->ch[0]);
    return last;
  case N_DEFER:
    defer_append(e, n);
    return last;
  case N_REGION:
    exec_region(e, n);
    return last;
  case N_RETURN: {
    Value v = void_value();
    if (n->n >= 1 && n->ch[0]) eval(e, n->ch[0], &v);
    e->ret = v;
    e->flow = FL_RETURN;
    return last;
  }
  case N_TRANSFER: {
    switch (n->tok.kind) {
    case T_KW_BREAK: e->flow = FL_BREAK; break;
    case T_KW_CONTINUE: e->flow = FL_CONTINUE; break;
    default:
      vfail(e, n, "`yield` is not executable in the bootstrap phase (no coroutines)");
    }
    e->label = n->n >= 1 && n->ch[0]
      ? savestr(e, ttext(n->ch[0]), n->ch[0]->tok.len) : NULL;
    return last;
  }
  case N_LABEL: {
    const char *lab = savestr(e, ttext(n->ch[0]), n->ch[0]->tok.len);
    exec_stmt(e, n->ch[1]);
    if ((e->flow == FL_BREAK || e->flow == FL_CONTINUE) && e->label &&
        strcmp(e->label, lab) == 0)
      e->flow = FL_NONE;
    return last;
  }
  default:
    vfail(e, n, "`%s` is not an executable statement", node_kind_name(n->k));
  }
}

/* A block is a scope: a fresh frame for its statements, popped when the
 * block ends, and the value of its last statement is its tail value. */
static Value exec_block(Exec *e, Node *blk) {
  Env *fr = env_new(e, e->cur);
  Env *saved = e->cur;
  e->cur = fr;
  size_t mark = e->nd;
  Value last = void_value();
  for (int i = 0; i < blk->n && e->flow == FL_NONE; i++)
    last = exec_stmt(e, blk->ch[i]);
  if (e->flow == FL_NONE) {
    defer_run_from(e, mark);
    e->nd = mark;
  }
  e->cur = saved;
  return last;
}

/* ---------------- function invocation ---------------- */

static Value invoke_user(Exec *e, Func f, Value *args, int narg, const Node *at) {
  Env *fr = env_new(e, f.closure);
  Node *pl = f.params;                    /* N_LIST, may be NULL */
  size_t np = pl ? pl->n : 0;
  if (narg > (int)np)
    vfail(e, at, "too many arguments for `%s` (%d given, %zu expected)",
          f.name, narg, np);
  for (size_t i = 0; i < np; i++) {
    Node *pa = pl->ch[i];                 /* N_PARAM: ch[0] name, ch[2] default */
    Value v;
    if ((int)i < narg) v = args[i];
    else {
      if (!pa->ch[2])
        vfail(e, pa, "missing argument for parameter %zu of `%s`", i + 1, f.name);
      eval(e, pa->ch[2], &v);
    }
    Node *pname = pa->ch[0];
    if (pname && !(pname->tok.len == 1 && pname->tok.start[0] == '_'))
      env_bind(e, fr, savestr(e, ttext(pname), pname->tok.len), v);
  }
  Env *saved_cur = e->cur, *saved_fn = e->fnframe;
  e->cur = fr;
  e->fnframe = fr;
  e->flow = FL_NONE;
  size_t mark = e->nd;                    /* function-level defer scope */
  Value ret = void_value();
  Node *body = f.body;
  if (body) {
    if (body->k == N_BLOCK) {
      Value imp = exec_block(e, body);
      ret = e->flow == FL_RETURN ? e->ret : imp;
    } else {
      Value imp = exec_stmt(e, body);
      ret = e->flow == FL_RETURN ? e->ret : imp;
    }
  }
  e->flow = FL_NONE;
  defer_run_from(e, mark);                /* run the function's defers */
  e->nd = mark;
  e->cur = saved_cur;
  e->fnframe = saved_fn;
  return ret;
}

/* ---------------- builtin `out` module & top level ---------------- */

static void out_set(Exec *e, Field *fs, const char *nm, BuiltinFn fn, int tag) {
  Func f;
  memset(&f, 0, sizeof f);
  f.kind = F_BUILTIN;
  f.tag = tag;
  f.name = nm;
  f.fn = fn;
  fs->nm = nm;
  fs->v = func_value(e, f);
}

static Value exec_toplevel_item(Exec *e, Node *d) {
  switch (d->k) {
  case N_MODULE: case N_IMPORT: case N_DIRECTIVE:
  case N_INTERFACE_DECL: case N_ERROR_DECL:
    return void_value();                  /* accepted, not executed */
  case N_STRUCT_DECL:
    reg_struct(e, d);
    return void_value();
  case N_ENUM_DECL: {
    reg_enum(e, d);
    Node *body = d->n >= 2 ? d->ch[1] : NULL;
    if (body && body->k == N_LIST)
      for (int j = 0; j < body->n; j++) {
        Node *mem = body->ch[j];
        Node *mn = mem->ch[0];
        if (!mn) continue;
        Value ev;
        memset(&ev, 0, sizeof ev);
        ev.k = V_ENUM;
        ev.en.name = savestr(e, ttext(mn), mn->tok.len);
        ev.en.tag = j;
        env_bind(e, e->global, ev.en.name, ev);
      }
    return void_value();
  }
  case N_FUNC_DECL: {
    Node *q = d->ch[0];                   /* N_NAME | N_QNAME */
    Node *nm = q->k == N_QNAME ? q->ch[1] : q;
    Node *params = NULL, *body = NULL;
    for (int i = 0; i < d->n; i++) {
      Node *c = d->ch[i];
      if (c->k == N_LIST && !params) params = c;
    }
    body = d->n >= 1 ? d->ch[d->n - 1] : NULL;
    /* a trailing ret type (no body) can't be a function here */
    Func f;
    memset(&f, 0, sizeof f);
    f.kind = F_USER;
    f.name = savestr(e, ttext(nm), nm->tok.len);
    f.params = params;
    f.body = body;
    f.closure = e->global;
    env_bind(e, e->global, f.name, func_value(e, f));
    return void_value();
  }
  default:
    return exec_stmt(e, d);               /* loose top-level statements */
  }
}

/* ---------------- entry point ---------------- */

int exec_run(const char *src, Node *root, char *msg, size_t msgn,
             int *line, int *col) {
  Exec e;
  memset(&e, 0, sizeof e);
  e.src = src;
  e.depth_max = 1024;
  /* The arena struct and the diagnostic block must live in the heap
   * *before* the setjmp: arena_alloc mutates *e.ar and vfail writes
   * *e.errb (both heap), while the autoc locals of exec_run — the
   * function containing the setjmp — are indeterminate after a longjmp
   * (C11 §7.13.2.1).  `e.ar` and `e.errb` are assigned exactly once,
   * before the setjmp, and never reassigned, so both are safe to read
   * (and, for the arena, to free) on the recovery path. */
  e.ar = malloc(sizeof *e.ar);
  if (!e.ar) return -1;
  memset(e.ar, 0, sizeof *e.ar);
  e.errb = xalloc(&e, sizeof *e.errb);
  if (setjmp(e.abort) != 0) {
    /* Panic = abort: no destructors run, but the arena is well-formed
     * heap state (see above), so it is released rather than abandoned. */
    if (msg && msgn) snprintf(msg, msgn, "%s", e.errb->msg);
    if (line) *line = e.errb->line;
    if (col) *col = e.errb->col;
    arena_free(e.ar);
    free(e.ar);
    return -1;
  }
  e.global = env_new(&e, NULL);
  e.cur = e.global;
  e.fnframe = e.global;

  /* the `out` module: println / print / error builtins */
  Value om;
  memset(&om, 0, sizeof om);
  om.k = V_REC;
  om.rec.n = 3;
  om.rec.tyname = "out";
  om.rec.fs = xalloc(&e, om.rec.n * sizeof *om.rec.fs);
  out_set(&e, &om.rec.fs[0], "println", bi_out_println, 0);
  out_set(&e, &om.rec.fs[1], "print", bi_out_print, 0);
  out_set(&e, &om.rec.fs[2], "error", bi_out_error, 0);
  env_bind(&e, e.global, "out", om);

  if (!root) vfail(&e, NULL, "no program");
  if (root->k == N_FILE)
    for (int i = 0; i < root->n; i++)
      exec_toplevel_item(&e, root->ch[i]);
  else
    exec_toplevel_item(&e, root);

  Value mv;
  int code = 0;
  if (env_lookup(e.global, "main", &mv) && mv.k == V_FUNC &&
      mv.fn.f.kind == F_USER) {
    Value ret = invoke_user(&e, mv.fn.f, NULL, 0, NULL);
    if (ret.k == V_INT) code = (int)ret.i;
  }
  arena_free(e.ar);
  free(e.ar);
  return code;
}
