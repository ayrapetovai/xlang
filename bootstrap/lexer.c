#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"

/*
 * Lexical rules implemented here, from GRAMMAR.md section 2:
 *
 *  - the universal separator (2.5): newline, ';' and ',' all become one
 *    T_SEP token; consecutive separators collapse to a single one (lists
 *    accept `X { Sep X }` with a trailing Sep, section 1).
 *  - line continuation (1/2): a newline is *suppressed*, not turned into
 *    a T_SEP, when the next source line begins with an operator symbol
 *    (dot, plus, &&, ==, ->, comma, and so on).
 *  - comments (2.1): line comments and nested block comments; the closing
 *    run of stars complements the opening run (a two-star opener needs a
 *    two-star closer).
 *  - names and keywords (2.2): `type field value pointer any` are NOT
 *    keywords - they stay T_ID and the parser disambiguates them.
 *  - literals (2.3): no-sign integers (dec/hex/bin), floats
 *    `Digit{Digit} "." Digit{Digit}`, chars and strings with escapes.
 *  - operators (2.4): longest match first, including the `:=` of 4.1;
 *    `++` and `--` are single diagnostic tokens ("no ++/--", README).
 */

/* ---------------- keywords (§2.2) ---------------- */

struct kw { const char *name; TokKind kind; };

static const struct kw keywords[] = {
  { "if", T_KW_IF },        { "then", T_KW_THEN },
  { "else", T_KW_ELSE },    { "loop", T_KW_LOOP },
  { "do", T_KW_DO },        { "until", T_KW_UNTIL },
  { "match", T_KW_MATCH },  { "select", T_KW_SELECT },
  { "default", T_KW_DEFAULT },
  { "try", T_KW_TRY },      { "catch", T_KW_CATCH },
  { "defer", T_KW_DEFER },  { "spawn", T_KW_SPAWN },
  { "return", T_KW_RETURN },{ "break", T_KW_BREAK },
  { "continue", T_KW_CONTINUE }, { "yield", T_KW_YIELD },
  { "in", T_KW_IN },        { "is", T_KW_IS },
  { "const", T_KW_CONST },  { "module", T_KW_MODULE },
  { "import", T_KW_IMPORT },
  { "struct", T_KW_STRUCT },{ "enum", T_KW_ENUM },
  { "func", T_KW_FUNC },    { "interface", T_KW_INTERFACE },
  { "error", T_KW_ERROR },
  { "void", T_KW_VOID },    { "byte", T_KW_BYTE },
  { "char", T_KW_CHAR },    { "int", T_KW_INT },
  { "uint", T_KW_UINT },    { "float", T_KW_FLOAT },
  { "bool", T_KW_BOOL },    { "string", T_KW_STRING },
  { "bytes", T_KW_BYTES },  { "true", T_KW_TRUE },
  { "false", T_KW_FALSE },
};

static TokKind keyword_of(const char *s, size_t n) {
  for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
    if (strlen(keywords[i].name) == n &&
        memcmp(keywords[i].name, s, n) == 0)
      return keywords[i].kind;
  }
  return T_ID;
}

/* ---------------- operators (§2.4), longest first ---------------- */

struct op { const char *s; TokKind kind; };

static const struct op ops[] = {
  { "<<~", T_OP_SHL_CY },   { ">>~", T_OP_SHR_CY },   { ">>>", T_OP_USHR },
  { "<<=", T_OP_SHL_ASSIGN }, { ">>=", T_OP_SHR_ASSIGN },
  { "..=", T_OP_RANGE_LE }, { "..<", T_OP_RANGE_LT },
  { "->", T_OP_ARROW },     { "=>", T_OP_FATARROW },  { "<>", T_OP_SWAP },
  { "<=", T_OP_LE },        { ">=", T_OP_GE },        { "==", T_OP_EQ },
  { "!=", T_OP_NE },        { "&&", T_OP_AND },       { "||", T_OP_OR },
  { "??", T_OP_QQ },        { "<-", T_OP_RECV },
  { "<<", T_OP_SHL },       { ">>", T_OP_SHR },
  { "+=", T_OP_ADD_ASSIGN },{ "-=", T_OP_SUB_ASSIGN },
  { "*=", T_OP_MUL_ASSIGN },{ "/=", T_OP_DIV_ASSIGN },
  { "%=", T_OP_MOD_ASSIGN },
  { "&=", T_OP_AND_ASSIGN },{ "|=", T_OP_OR_ASSIGN }, { "^=", T_OP_XOR_ASSIGN },
  { ":=", T_OP_DEFINE },
  { "++", T_OP_INC },       { "--", T_OP_DEC },
  { "#", T_HASH },
  { "(", T_LPAREN },        { ")", T_RPAREN },
  { "[", T_LBRACKET },      { "]", T_RBRACKET },
  { "{", T_LBRACE },        { "}", T_RBRACE },
  { ".", T_DOT },           { ":", T_COLON },
  { "=", T_OP_ASSIGN },     { "+", T_OP_PLUS },       { "-", T_OP_MINUS },
  { "*", T_OP_STAR },       { "/", T_OP_SLASH },      { "%", T_OP_PERCENT },
  { "&", T_OP_AMP },        { "|", T_OP_PIPE },       { "^", T_OP_CARET },
  { "~", T_OP_TILDE },      { "!", T_BANG },          { "?", T_OP_QMARK },
  { "<", T_OP_LT },         { ">", T_OP_GT },
};

/*
 * Does `p` begin an operator symbol? Used for line continuation (§1):
 * a line that begins with an operator continues the previous statement.
 */
static int operator_starts_here(const char *p) {
  switch (*p) {
    case '+': case '-': case '*': case '/': case '%':
    case '&': case '|': case '^': case '~': case '!': case '?':
    case '<': case '>': case '=': case ':': case '.': case ',':
      return 1;
    default:
      return 0;
  }
}

/* ---------------- position tracking ---------------- */

static void bump(Lexer *l, const char *to) {
  while (l->cur < to) {
    if (*l->cur == '\n') { l->line++; l->col = 1; }
    else l->col++;
    l->cur++;
  }
}

/* ---------------- comments (§2.1) ---------------- */

static void skip_line_comment(Lexer *l) {
  const char *e = l->cur;
  while (*e && *e != '\n') e++;
  bump(l, e);   /* the newline itself stays: it is a separator */
}

/*
 * Nested block comments. An opener is '/' followed by a run of stars; the
 * closer is a run of stars of at least that length followed by '/'
 * ("the closing literal complements by amount of stars"). Exact mixed-run
 * nesting is an open item (§2.1 / §9).
 */
static void skip_block_comment(Lexer *l) {
  int sstack[16];
  int sp = 0, depth;
  const char *p = l->cur + 1;   /* after '/' */

  while (*p == '*') p++;                       /* opening run */
  sstack[sp++] = (int)(p - (l->cur + 1));
  depth = 1;

  while (*p && depth) {
    if (p[0] == '/' && p[1] == '*') {          /* nested opener */
      int s = 1;
      p += 2;
      while (*p == '*') { s++; p++; }
      if (sp < (int)(sizeof sstack / sizeof sstack[0])) sstack[sp++] = s;
      depth++;
      continue;
    }
    if (p[0] == '*') {
      const char *q = p;
      while (*q == '*') q++;
      if (*q == '/' && (int)(q - p) >= sstack[sp - 1]) {
        p = q + 1;                             /* closer */
        sp--;
        depth--;
        continue;
      }
    }
    p++;
  }
  if (depth)
    snprintf(l->err, sizeof l->err, "unterminated block comment");
  bump(l, p);
}

/* ---------------- literals (§2.3) ---------------- */

/* dist: 0 = int, 1 = float (dec only; hex/bin are ints by construction) */
static TokKind scan_number(Lexer *l) {
  if (l->cur[0] == '0' && (l->cur[1] == 'x' || l->cur[1] == 'X')) {
    bump(l, l->cur + 2);
    while (isxdigit((unsigned char)*l->cur)) bump(l, l->cur + 1);
    return T_NUM;
  }
  if (l->cur[0] == '0' && (l->cur[1] == 'b' || l->cur[1] == 'B')) {
    bump(l, l->cur + 2);
    while (*l->cur == '0' || *l->cur == '1') bump(l, l->cur + 1);
    return T_NUM;
  }
  while (isdigit((unsigned char)*l->cur)) bump(l, l->cur + 1);
  /* FloatLiteral = Digit{Digit} "." Digit{Digit} — so `1.` and `a..<b`
     keep the '.' (int + range operator), while `1.0` is one float. */
  if (*l->cur == '.' && isdigit((unsigned char)l->cur[1])) {
    bump(l, l->cur + 1);
    while (isdigit((unsigned char)*l->cur)) bump(l, l->cur + 1);
  }
  return T_NUM;
}

static void scan_quoted(Lexer *l, int is_char) {
  char quote = is_char ? '\'' : '"';
  bump(l, l->cur + 1);                 /* opening quote */
  while (*l->cur && *l->cur != quote && *l->cur != '\n') {
    if (*l->cur == '\\' && l->cur[1]) bump(l, l->cur + 2);
    else bump(l, l->cur + 1);
  }
  if (*l->cur == quote) { bump(l, l->cur + 1); return; }
  snprintf(l->err, sizeof l->err, "unterminated %s literal",
           is_char ? "char" : "string");
}

/* ---------------- driver ---------------- */

void lexer_init(Lexer *l, const char *src) {
  l->cur = src;
  l->start = src;
  l->line = 1;
  l->col = 1;
  l->err[0] = '\0';
}

Token lexer_next(Lexer *l) {
  const char *sep_start = NULL;
  int sep_line = 0, sep_col = 0, have_sep = 0;

  for (;;) {
    while (*l->cur == ' ' || *l->cur == '\t' || *l->cur == '\r')
      bump(l, l->cur + 1);

    if (l->cur[0] == '/' && l->cur[1] == '/') { skip_line_comment(l); continue; }
    if (l->cur[0] == '/' && l->cur[1] == '*') {
      const char *cs = l->cur;
      int csl = l->line, csc = l->col;
      l->err[0] = '\0';
      skip_block_comment(l);
      if (l->err[0])
        return (Token){ T_UNDEF, cs, 2, csl, csc };
      continue;
    }

    char c = *l->cur;
    if (c == '\n' || c == ';' || c == ',') {
      /*
       * Line continuation: a newline after which the next line *begins with
       * an operator symbol* continues the statement — no T_SEP is emitted.
       */
      if (c == '\n' && !have_sep) {
        const char *p = l->cur + 1;
        while (*p == ' ' || *p == '\t') p++;
        if (operator_starts_here(p)) { bump(l, l->cur + 1); continue; }
      }
      /* collapse consecutive separators into one */
      if (!have_sep) {
        have_sep = 1;
        sep_start = l->cur;
        sep_line = l->line;
        sep_col = l->col;
      }
      bump(l, l->cur + 1);
      continue;
    }
    break;
  }

  if (have_sep)
    return (Token){ T_SEP, sep_start, (size_t)(l->cur - sep_start),
                    sep_line, sep_col };
  if (*l->cur == '\0')
    return (Token){ T_EOF, l->cur, 0, l->line, l->col };

  const char *s = l->cur;
  int sl = l->line, sc = l->col;

  if (isalpha((unsigned char)*s) || *s == '_') {
    while (isalnum((unsigned char)*l->cur) || *l->cur == '_')
      bump(l, l->cur + 1);
    return (Token){ keyword_of(s, (size_t)(l->cur - s)), s,
                    (size_t)(l->cur - s), sl, sc };
  }
  if (isdigit((unsigned char)*s)) {
    TokKind k = scan_number(l);
    return (Token){ k, s, (size_t)(l->cur - s), sl, sc };
  }
  if (*s == '"' || *s == '\'') {
    int is_char = (*s == '\'');
    scan_quoted(l, is_char);
    if (l->err[0])
      return (Token){ T_UNDEF, s, (size_t)(l->cur - s), sl, sc };
    return (Token){ is_char ? T_CHAR : T_STR, s,
                    (size_t)(l->cur - s), sl, sc };
  }

  size_t nops = sizeof ops / sizeof ops[0];
  for (size_t i = 0; i < nops; i++) {
    size_t n = strlen(ops[i].s);
    if (strncmp(s, ops[i].s, n) == 0) {
      bump(l, s + n);
      return (Token){ ops[i].kind, s, n, sl, sc };
    }
  }

  snprintf(l->err, sizeof l->err, "unexpected character '%c' (0x%02X)",
           *s, (unsigned char)*s);
  return (Token){ T_UNDEF, s, 1, sl, sc };
}