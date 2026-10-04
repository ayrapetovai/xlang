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
  size_t run_end;
  bool run_valid;
  // kind of the last significant token, so a '\n' can tell that the line ended
  // with an infix operator. Updated by parser_move_forward only, so the
  // continuation probe cannot perturb it.
  TokenKind prev_kind;
} Parser;

struct Parser *parser_new(struct Lexer*);

ASTNode *parser_parse(struct Parser *);

void parser_close(struct Parser*);

#endif
