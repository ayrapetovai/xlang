#ifndef LEXER_H
#define LEXER_H

#include "tokens.h"

typedef struct Lexer {
  char *base;
  char *current;
  int line;
  int col;
} Lexer;

struct Lexer* new_lexer();

struct Token* next_token(Lexer*);

#endif


