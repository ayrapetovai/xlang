#ifndef TOKENS_H
#define TOKENS_H

#include <stddef.h>

typedef enum TokenKind {
  TOK_UNDEF,
  TOK_SEP,
  TOK_ID,
  TOK_IF,
  TOK_IMPORT,
  TOK_NL,
  TOK_SPACE,
  TOK_FUNC,
  TOK_DOT,
} TokenKind;

typedef struct {
  enum TokenKind kind;
  const char* letters;
} TokenDef;

// filled in tokens.c
extern const TokenDef TOKEN_KEYWORDS[];
extern const size_t TOKEN_KEYWORDS_COUNT;

typedef struct Token {
  enum TokenKind kind;
  char value[128];
  size_t line;
  size_t col;
} Token;

void print_token(struct Token*);

#endif

