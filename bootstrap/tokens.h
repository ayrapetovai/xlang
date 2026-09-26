#ifndef TOKENS_H
#define TOKENS_H

#include <stddef.h>

typedef enum TokenKind {
  TOK_FUNC,
} TokenKind;

typedef struct Token {
  enum TokenKind kind;
  char value[128];
  size_t line;
  size_t col;
} Token;

void print_token(struct Token*);

#endif

