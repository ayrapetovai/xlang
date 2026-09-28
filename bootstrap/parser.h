#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef struct Parser {
  struct Lexer* lexer;
} Parser;

struct Parser *new_parser();

int parser_parse(struct Parser *);

#endif
