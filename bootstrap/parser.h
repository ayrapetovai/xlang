#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "tokens.h"
#include "lexer.h"

typedef struct Parser {
  struct Lexer* lexer;
  struct Token* current_token;
  struct Token** token_pool;
  size_t token_pool_size;
  size_t token_pool_cap;
  char error[1024];
  // what expr_token_matches found past a run of newlines, and where that run
  // started, so a repeat attempt at the same offset does not rescan it
  size_t nl_probe_start;
  TokenKind nl_probe_kind;
  bool nl_probe_valid;
} Parser;

struct Parser *parser_new(struct Lexer*);

ASTNode *parser_parse(struct Parser *);

void parser_close(struct Parser*);

#endif
