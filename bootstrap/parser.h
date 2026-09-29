#ifndef PARSER_H
#define PARSER_H

#include "tokens.h"
#include "lexer.h"

typedef struct Parser {
  struct Lexer* lexer;
  struct Token* current_token;
  char error[1024];
} Parser;

struct Parser *parser_new(struct Lexer*);

int parser_parse(struct Parser *);

#endif
