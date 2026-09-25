#pragma once

#include <stddef.h>

/*
 * Token model for the bootstrap compiler's lexer, derived from GRAMMAR.md
 * §2 (lexical grammar). Every token kind below maps to a terminal or
 * terminal class of the EBNF; nothing is invented here.
 */
typedef enum {
  T_EOF,
  T_SEP,                 /* newline | ';' | ',' — universal separator (§2.5)  */
  T_ID,                  /* identifier, incl. the contextual meta-type names  */
                         /* `type field value pointer any` stay T_ID; the    */
                         /* parser disambiguates their declaration/member     */
                         /* use (GRAMMAR.md §2.2 note).                       */
  T_NUM,                 /* integer or float literal (§2.3) — text preserved  */
  T_CHAR,                /* char literal, `'0'`, `'\n'`                       */
  T_STR,                 /* string literal, `"…"` with escapes + format specs */
  T_UNDEF,               /* lexer error sentinel (message in Lexer.err)       */

  /* --- unconditional keywords (§2.2, reserved as names) ------------------ */
  T_KW_IF, T_KW_THEN, T_KW_ELSE, T_KW_LOOP, T_KW_DO, T_KW_UNTIL,
  T_KW_MATCH, T_KW_SELECT, T_KW_DEFAULT,
  T_KW_TRY, T_KW_CATCH,
  T_KW_DEFER, T_KW_SPAWN, T_KW_RETURN, T_KW_BREAK, T_KW_CONTINUE, T_KW_YIELD,
  T_KW_IN, T_KW_IS, T_KW_CONST,
  T_KW_MODULE, T_KW_IMPORT,
  T_KW_STRUCT, T_KW_ENUM, T_KW_FUNC, T_KW_INTERFACE,
  T_KW_ERROR,
  T_KW_VOID, T_KW_BYTE, T_KW_CHAR, T_KW_INT, T_KW_UINT, T_KW_FLOAT,
  T_KW_BOOL, T_KW_STRING, T_KW_BYTES,
  T_KW_TRUE, T_KW_FALSE,

  /* --- punctuation and operators (§2.4) ----------------------------------- */
  T_HASH,                /* `#`  (directive prefix, §3.2)                     */
  T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
  T_DOT, T_COLON,        /* `.`  `:`                                            */
  T_OP_ASSIGN,           /* `=`   (also field defaults / named args)          */
  T_OP_DEFINE,           /* `:=`  (VariableDecl, §4.1)                        */
  T_OP_SWAP,             /* `<>`                                               */
  T_OP_AMP,              /* `&`   (address-of / move-in)                      */
  T_OP_PIPE, T_OP_CARET, T_OP_TILDE,            /* `|` `^` `~`               */
  T_OP_PLUS, T_OP_MINUS, T_OP_STAR, T_OP_SLASH, T_OP_PERCENT,
  T_OP_SHL, T_OP_SHR, T_OP_USHR, T_OP_SHL_CY, T_OP_SHR_CY,
                                          /* `<<` `>>` `>>>` `<<~` `>>~`      */
  T_OP_AND, T_OP_OR,     /* `&&` `||`                                          */
  T_BANG,                /* `!`  unary not / postfix unwrap                   */
  T_OP_QMARK, T_OP_QQ,   /* `?`  postfix maybe-unwrap;  `??` fallback         */
  T_OP_LT, T_OP_GT, T_OP_LE, T_OP_GE, T_OP_EQ, T_OP_NE,
  T_OP_RANGE_LE, T_OP_RANGE_LT,                /* `..=` `..<`                 */
  T_OP_ARROW, T_OP_FATARROW,                  /* `->` (§9 open)  `=>`         */
  T_OP_RECV,             /* `<-`                                               */
  T_OP_ADD_ASSIGN, T_OP_SUB_ASSIGN, T_OP_MUL_ASSIGN, T_OP_DIV_ASSIGN,
  T_OP_MOD_ASSIGN, T_OP_SHL_ASSIGN, T_OP_SHR_ASSIGN,
  T_OP_AND_ASSIGN, T_OP_OR_ASSIGN, T_OP_XOR_ASSIGN,  /* `&=` `|=` `^=`       */

  /* --- diagnostic tokens, NOT grammar terminals --------------------------- */
  /* README: "no `++`/`--` — use `i += 1`". Lexed as single tokens so the    */
  /* parser can reject them with a precise message instead of a nonsense     */
  /* double-operator parse failure.                                          */
  T_OP_INC, T_OP_DEC,

  T_KIND_COUNT
} TokKind;

typedef struct {
  TokKind kind;
  const char *start;     /* pointer into the source buffer                   */
  size_t len;
  int line, col;         /* 1-based position of `start`                      */
} Token;

const char *token_kind_name(TokKind kind);