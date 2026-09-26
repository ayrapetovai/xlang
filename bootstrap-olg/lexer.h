#pragma once

#include "tokens.h"

/*
 * Bootstrap lexer. Consumes one token per call; the token's `start` points
 * into the source buffer. Position is 1-based line/col.
 */
typedef struct Lexer {
  const char *cur;   /* next char to scan                                  */
  const char *start; /* first byte of the source buffer (error messages)   */
  int line, col;     /* position of *cur                                   */
  char err[128];     /* diagnostic; set when a T_UNDEF token is produced   */
} Lexer;

void lexer_init(Lexer *l, const char *src);
Token lexer_next(Lexer *l);