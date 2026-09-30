#ifndef PARSER_H
#define PARSER_H

#include "tokens.h"
#include "lexer.h"

typedef struct Parser {
  struct Lexer* lexer;
  struct Token* current_token;
  struct Token** token_pool;
  size_t token_pool_size;
  size_t token_pool_cap;
  char error[1024];
} Parser;

struct Parser *parser_new(struct Lexer*);

int parser_parse(struct Parser *);

void parser_close(struct Parser*);

#endif
