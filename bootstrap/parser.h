#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "tokens.h"
#include "lexer.h"

typedef struct Parser {
  struct Lexer* lexer;
  struct Token* current_token; // borrowed from the Lexer's token list, which owns it
  char error[1024];
  size_t nl_probe_start;
  TokenKind nl_probe_kind;
  bool nl_probe_valid;
  struct Token* deepest_tok; // furthest point reached during parsing, for error reporting
  size_t deepest_mark;       // the index of token after the deepest_tok
} Parser;

struct Parser *parser_new(struct Lexer*);

ASTNode *parser_parse(struct Parser *);

void parser_close(struct Parser*);

#endif
