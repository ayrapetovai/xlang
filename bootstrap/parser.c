#include "parser.h"
#include "lexer.h"
#include "tokens.h"
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

struct GrammarRule expr = {{
  PROD( TERM(TOK_LPAREN), NTRM(expr), TERM(TOK_RPAREN),           REDUCE(reduce_lparen_expr_rparen) ),
  PROD( NTRM(expr), TERM(TOK_PLUS), NTRM(expr),                   REDUCE(reduce_expr_plus_expr) ),
  PROD( TERM(TOK_NUMBER_L),                                       REDUCE(reduce_number_l) ),
}};

// WARNING: program :: the parsing entry point 
struct GrammarRule prog = {{
  PROD( NTRM(expr),                                               REDUCE(reduce_expr) ),
}};

// private functions

static void* parse(struct Parser* parser, struct GrammarRule* prod);
static bool parser_move_forward(struct Parser* parser);

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
  struct GrammarRule* rule = start;
  while (true) {

    if (parser->current_token == NULL && !parser_move_forward(parser)) {
      return NULL;
    }

    for (int i = 0; rule->productions[i].exists; i++) {
      struct Production* production = &rule->productions[i]; 

      void *params[PRODUCTION_MAX_LENGTH] = {};

      size_t param_count = 0;
      for (int j = 0; production->nodes[j].type != PN_EMPTY; j++) {
        struct ProdNode *node = &production->nodes[j];
        switch (node->type) {
          case PN_TERM:
            if (parser->current_token->kind == node->tok_kind){
              params[param_count] = parser->current_token; // accept (aka shift)
              if (!parser_move_forward(parser)) {
                return NULL;
              }
            } else goto try_next_production; // rollabck
            break;
          case PN_REDUCE:
            void* production_result = node->reducer(param_count, params);
            for (int k = 0; i < PRODUCTION_MAX_LENGTH && params[k] != NULL; k++)
              if (production_result != params[k]) // do not free the thing that we return to caller
                free(params[k]);
            return production_result;
          case PN_RULE:
            void *sub_rule_result = parse(parser, node->rule);
            if (sub_rule_result == NULL) {
              // error
              return NULL;
            }
            params[param_count] = sub_rule_result;
            break;
          default:
            // error
        }
        param_count += 1;
      }

      try_next_production:

      for (int i = 0; i < PRODUCTION_MAX_LENGTH && params[i] != NULL; i++)
        free(params[i]);
    }
  }
}

static bool parser_move_forward(struct Parser* parser) {
  enum LexState lex_state;
  parser->current_token = malloc(sizeof(struct Token));
  lex_state = lexer_next_token(parser->lexer, parser->current_token);

  if (lex_state == LEX_ERROR || lex_state == LEX_PRG_ERROR) {
    sprintf(parser->error, "parsing failed: %s", parser->lexer->error);
    return false;
  }

  return true;
}
