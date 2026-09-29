#include "parser.h"
#include "lexer.h"
#include "tokens.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef enum ProdNodeType {
  PN_EMPTY = 0,
  PN_TERM,
  PN_RULE,
  PN_REDUCE,
} ProdNodeType;

struct GrammarRule;

struct ProdNode {
  enum ProdNodeType type;
  union {
    enum TokenKind tok_kind;
    struct GrammarRule* rule;
    void* (*reducer)(size_t, void*[]); // pointer to function:   void* reducer(size_t argc, void* argv[])
  };
} ProdNode;

#define PRODUCTIONS_MAX    10
#define PRODUCTION_MAX_LENGTH 10

struct Production {
  bool exists;
  struct ProdNode nodes[PRODUCTION_MAX_LENGTH];
};

struct GrammarRule {
  struct Production productions[PRODUCTIONS_MAX];
};

#define TERM(t)   (struct ProdNode)   { .type = PN_TERM,   .tok_kind =  t }
#define NTRM(r)   (struct ProdNode)   { .type = PN_RULE,   .rule     = &r }
#define REDUCE(f) (struct ProdNode)   { .type = PN_REDUCE, .reducer  =  f }
#define PROD(...) (struct Production) { true, { __VA_ARGS__ }}

// Rule section.

// Rule structure:
//
// GrammarRule_1 name ::= {
//   Production_n { (term|nonterm)+ },
//   ...,
//   Production_n { (term|nonterm)+ },
// }
//
// grammar_2 ...
//

static void* reduce_number_l(size_t argc, void* argv[]) {
  assert(argc == 1);
  struct Token* tok = (struct Token*) argv[0];
  assert(tok->kind == TOK_NUMBER_L);
  int *value = malloc(sizeof(int));
  *value = atoi(tok->value);
  return value;
}

static void* reduce_expr_plus_expr(size_t argc, void* argv[]) {
  assert(argc == 3);
  *((int*) argv[0]) += *((int*) argv[2]);
  return argv[0];
}

static void* reduce_expr_minus_expr(size_t argc, void* argv[]) {
  assert(argc == 3);
  *((int*) argv[0]) -= *((int*) argv[2]);
  return argv[0];
}

static void* reduce_lparen_expr_rparen(size_t argc, void* argv[]) {
  assert(argc == 3);
  return argv[1];
}

static void* reduce_expr(size_t argc, void* argv[]) {
  assert(argc == 1);
  return argv[0];
}

// WARNING: expression :: arithmetics, logics, array access and funcfion calls
struct GrammarRule expr;
struct GrammarRule expr_term;

struct GrammarRule expr_term = {{
  PROD( TERM(TOK_NUMBER_L),                                     REDUCE(reduce_number_l) ),
  PROD( TERM(TOK_LPAREN), NTRM(expr), TERM(TOK_RPAREN),         REDUCE(reduce_lparen_expr_rparen) ),
}};

struct GrammarRule expr = {{
  PROD( NTRM(expr_term), TERM(TOK_PLUS), NTRM(expr),            REDUCE(reduce_expr_plus_expr) ),
  PROD( NTRM(expr_term), TERM(TOK_MINUS), NTRM(expr),           REDUCE(reduce_expr_minus_expr) ),
  PROD( NTRM(expr_term),                                        REDUCE(reduce_expr) ),
}};

// WARNING: program :: the parsing entry point 
struct GrammarRule prog = {{
  PROD( NTRM(expr),                                             REDUCE(reduce_expr) ),
}};

// private functions

static void* parse(struct Parser* parser, struct GrammarRule* prod);
static bool parser_move_forward(struct Parser* parser);

// A parse checkpoint captures everything the parser + lexer + reader need to
// rewind after a failed production attempt (real backtracking).
typedef struct ParseCheckpoint {
  size_t r_pos;
  size_t r_available;
  char r_unget[4];
  int lex_line;
  int lex_col;
  struct Token* current_token;
} ParseCheckpoint;

static void parser_checkpoint(struct Parser* parser, struct ParseCheckpoint* chk) {
  Reader* r = parser->lexer->reader;
  chk->r_pos = r->pos;
  chk->r_available = r->available;
  memcpy(chk->r_unget, r->unget_buf, sizeof r->unget_buf);
  chk->lex_line = parser->lexer->line;
  chk->lex_col = parser->lexer->col;
  chk->current_token = parser->current_token;
}

static void parser_restore(struct Parser* parser, const struct ParseCheckpoint* chk) {
  Reader* r = parser->lexer->reader;
  r->pos = chk->r_pos;
  r->available = chk->r_available;
  memcpy(r->unget_buf, chk->r_unget, sizeof r->unget_buf);
  parser->lexer->line = chk->lex_line;
  parser->lexer->col = chk->lex_col;
  parser->current_token = chk->current_token;
}

// public methods

struct Parser* parser_new(struct Lexer *lexer) {
  struct Parser *parser = malloc(sizeof(Parser));
  if (parser == NULL) return NULL;

  parser->current_token = NULL;
  memset(parser->error, '\0', sizeof(parser->error));

  if (lexer == NULL)
    sprintf(parser->error, "lexer is NULL");
  else
    parser->lexer = lexer;

  return parser;
}

int parser_parse(struct Parser *parser) {
  struct GrammarRule* start = &prog;
  void* parse_result = parse(parser, start);

  if (parse_result == NULL) return 1;

  int result = *((int*) parse_result);
  printf("result: %d\n", result);

  free(parse_result);

  return 0;
}

static void* parse(struct Parser* parser, struct GrammarRule* start) {
  if (parser->current_token == NULL && !parser_move_forward(parser)) {
    return NULL; // hard error
  }

  for (int i = 0; start->productions[i].exists; i++) {
    struct Production* production = &start->productions[i];

    struct ParseCheckpoint chk;
    parser_checkpoint(parser, &chk);

    void *params[PRODUCTION_MAX_LENGTH] = {};
    size_t param_count = 0;
    bool failed = false;

    for (int j = 0; production->nodes[j].type != PN_EMPTY; j++) {
      struct ProdNode *node = &production->nodes[j];
      switch (node->type) {
        case PN_TERM:
          if (parser->current_token->kind == node->tok_kind) {
            params[param_count] = parser->current_token; // accept (aka shift)
            if (!parser_move_forward(parser)) {
              return NULL; // hard error
            }
          } else {
            failed = true; // roll back to the next alternative
          }
          break;
        case PN_RULE:
          void *sub_rule_result = parse(parser, node->rule);
          if (sub_rule_result == NULL) {
            failed = true; // sub-rule did not match: try next production
          } else {
            params[param_count] = sub_rule_result;
          }
          break;
        case PN_REDUCE:
          return node->reducer(param_count, params);
        default:
          failed = true; // error
      }
      if (failed) break;
      param_count += 1;
    }

    parser_restore(parser, &chk); // rollback: rewind input + current token
  }

  return NULL; // no production in this rule accepts the current token
}

static bool parser_move_forward(struct Parser* parser) {
  enum LexState lex_state;
  parser->current_token = malloc(sizeof(struct Token));
  memset(parser->current_token, 0, sizeof(struct Token)); // no garbage kind

  lex_state = lexer_next_token(parser->lexer, parser->current_token);

  if (lex_state == LEX_ERROR || lex_state == LEX_PRG_ERROR) {
    sprintf(parser->error, "parsing failed: %s", parser->lexer->error);
    return false;
  }

  if (lex_state == LEX_EOF) {
    // sentinel: matches no terminal, so rules fail cleanly at end of input
    parser->current_token->kind = TOK_UNDEF;
  }

  return true;
}
