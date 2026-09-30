#include "parser.h"
#include "reader.h"
#include "lexer.h"
#include "logger.h"
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
  const char* name;
  const enum ProdNodeType type;
  const union {
    enum TokenKind tok_kind;
    struct GrammarRule* rule;
    void* (*reducer)(size_t, void*[]); // pointer to function:   void* reducer(size_t argc, void* argv[])
  };
} ProdNode;

#define PRODUCTIONS_MAX    10
#define PRODUCTION_MAX_LENGTH 10

struct Production {
  const bool exists;
  const struct ProdNode nodes[PRODUCTION_MAX_LENGTH];
};

struct GrammarRule {
  const char* name;
  const struct Production productions[PRODUCTIONS_MAX];
};

#define TERM(t)   (const struct ProdNode)   { .name = #t, .type = PN_TERM,   .tok_kind =  t }
#define NTRM(r)   (const struct ProdNode)   { .name = #r, .type = PN_RULE,   .rule     = &r }
#define REDUCE(f) (const struct ProdNode)   { .name = #f, .type = PN_REDUCE, .reducer  =  f }
#define PROD(...) (const struct Production) { true, { __VA_ARGS__ }}
#define RULE(rule_id, ...) struct GrammarRule rule_id = { .name = #rule_id, .productions = { __VA_ARGS__ } };

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

static void* reduce_minus_expr(size_t argc, void* argv[]) {
  assert(argc == 2);
  *((int*) argv[1]) = - *((int*) argv[1]);
  return argv[1];
}

static void* reduce_expr_star_expr(size_t argc, void* argv[]) {
  assert(argc == 3);
  *((int*) argv[0]) *= *((int*) argv[2]);
  return argv[0];
}

static void* reduce_expr_slash_expr(size_t argc, void* argv[]) {
  assert(argc == 3);
  *((int*) argv[0]) /= *((int*) argv[2]);
  return argv[0];
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

static void* reduce_echo_expr(size_t argc, void* argv[]) {
  assert(argc == 2);
  printf("%d\n", *((int*)argv[1]));
  return argv[1];
}

struct GrammarRule expr;

//**************************************************************
// expression :: arithmetics, logics, if, match, array access and funcfion calls
RULE( expr_factor,
  PROD( TERM(TOK_NUMBER_L),                                     REDUCE(reduce_number_l) ),
  PROD( TERM(TOK_LPAREN), NTRM(expr), TERM(TOK_RPAREN),         REDUCE(reduce_lparen_expr_rparen) ),
  PROD( TERM(TOK_MINUS),  NTRM(expr),                           REDUCE(reduce_minus_expr) ),
)

RULE( expr_term,
  PROD( NTRM(expr_factor), TERM(TOK_STAR),  NTRM(expr_term),    REDUCE(reduce_expr_star_expr) ),
  PROD( NTRM(expr_factor), TERM(TOK_SLASH), NTRM(expr_term),    REDUCE(reduce_expr_slash_expr) ),
  PROD( NTRM(expr_factor),                                      REDUCE(reduce_expr) ),
)

RULE( expr,
  PROD( NTRM(expr_term), TERM(TOK_PLUS),  NTRM(expr),           REDUCE(reduce_expr_plus_expr) ),
  PROD( NTRM(expr_term), TERM(TOK_MINUS), NTRM(expr),           REDUCE(reduce_expr_minus_expr) ),
  PROD( NTRM(expr_term),                                        REDUCE(reduce_expr) ),
)

//**************************************************************
// program :: the parsing entry point
RULE( prog,
  PROD( TERM(TOK_ECHO), NTRM(expr),                             REDUCE(reduce_echo_expr) ),
  PROD( NTRM(expr),                                             REDUCE(reduce_expr) ),
)

// private functions

static void* parse_by_rule(struct Parser* parser, struct GrammarRule* rule);
static bool parser_move_forward(struct Parser* parser);

// A parse checkpoint captures everything the parser + lexer + reader need to
// rewind after a failed production attempt (real backtracking).
typedef struct ParseCheckpoint {
  size_t r_pos;
  size_t r_available;
  char r_unget[READER_UNGET_BUF_SIZE];
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
  parser->token_pool = NULL;
  parser->token_pool_size = 0;
  parser->token_pool_cap = 0;
  memset(parser->error, '\0', sizeof(parser->error));

  if (lexer == NULL)
    sprintf(parser->error, "lexer is NULL");
  else
    parser->lexer = lexer;

  return parser;
}

int parser_parse(struct Parser *parser) {
  void* parse_result = parse_by_rule(parser, &prog);
  int rc = 1;

  if (parse_result != NULL) {
    // int result = *((int*) parse_result);
    // printf("result: %d\n", result);

    free(parse_result); // the root result belongs to parser_parse
    rc = 0;
  }

  return rc;
}

void parser_close(struct Parser* parser) {
  if (parser == NULL) return;

  for (size_t i = 0; i < parser->token_pool_size; i++)
    free(parser->token_pool[i]);
  free(parser->token_pool);
  parser->token_pool = NULL;
  parser->token_pool_size = 0;

  free(parser);
}

static bool starts_with_same_nodes(const struct Production *p1, const struct Production *p2, size_t to) {
  bool equal = true;
  size_t i = 0;
  while (i < to) {
    struct ProdNode n1 = p1->nodes[i];
    struct ProdNode n2 = p2->nodes[i];
    if ( (n1.type != n2.type)
      || (n1.type == PN_RULE && n2.type == PN_RULE && n1.rule != n2.rule)
      || (n1.type == PN_TERM && n2.type == PN_TERM && n1.tok_kind != n2.tok_kind)
      || (strcmp(n1.name, n2.name)) ) {
      equal = false;
      break;
    }
    i++;
  }
  return equal;
}

static void* parse_by_rule(struct Parser* parser, struct GrammarRule* rule) {
  if (rule == NULL) return NULL; // hard error
  if (parser->current_token == NULL && !parser_move_forward(parser)) return NULL; // hard error

  void *params[PRODUCTION_MAX_LENGTH] = {};
  bool param_is_token[PRODUCTION_MAX_LENGTH] = {}; // true: Token* (lookahead), false: reduced result
  size_t param_count = 0;

  int i = 0;
  int j = 0;
production_cycle:
  for (; rule->productions[i].exists; i++) {
    const struct Production* production = &rule->productions[i];

    struct ParseCheckpoint chk;
    parser_checkpoint(parser, &chk);

    bool failed = false;

    LOG_DEBUG("using rule ::%s:: #%d:%d", rule->name, i, j);

    for (; production->nodes[j].type != PN_EMPTY; j++) {
      const struct ProdNode *prod_node = &production->nodes[j];
      switch (prod_node->type) {
        case PN_TERM:
          if (parser->current_token->kind == prod_node->tok_kind) {
            LOG_DEBUG("accept %s = %s", prod_node->name, parser->current_token->value);
            params[param_count] = parser->current_token;
            param_is_token[param_count] = true;
            if (!parser_move_forward(parser))
              return NULL; // hard error
          } else if (rule->productions[i + 1].exists
            && starts_with_same_nodes(&rule->productions[i], &rule->productions[i + 1], i)
          ) {
            LOG_DEBUG("reject %s = %s", prod_node->name, parser->current_token->value);
            LOG_DEBUG("try next ::%s:: #%d -> #%d", rule->name, i, i + 1);
            i += 1;
            // move to next production without rollback if it starts with the same nodes
            goto production_cycle;
          } else {
            failed = true; // rollback
          }
          break;
        case PN_RULE:
          void *sub_rule_result = parse_by_rule(parser, prod_node->rule);
          if (sub_rule_result == NULL)
            failed = true; // sub-rule did not match: try next production
          else {
            params[param_count] = sub_rule_result;
            param_is_token[param_count] = false;
          }
          LOG_DEBUG("continue rule ::%s:: #%d:%d", rule->name, i, j);
          break;
        case PN_REDUCE:
          LOG_DEBUG("reduce rule ::%s:: #%d:%d", rule->name, i, j);
          void* reduce_result = prod_node->reducer(param_count, params);
          // free only the results this frame consumed; tokens are owned by
          // the token pool and must outlive any live ParseCheckpoint
          for (size_t k = 0; k < param_count; k++)
            if (!param_is_token[k] && params[k] != reduce_result)
              free(params[k]);
          return reduce_result;
        default:
          failed = true; // error
      }
      if (failed) break;
      param_count += 1;
    }
    j = 0;

    LOG_DEBUG("rollback rule ::%s:: #%d", rule->name, i);

    // successive procesing must have been returned the reduced result, so this is an error handling
    // rollback: free consumed results only; rewind input + current token
    for (size_t k = 0; k < param_count; k++)
      if (!param_is_token[k]) free(params[k]);
    memset(params, 0, PRODUCTION_MAX_LENGTH * sizeof(*params));
    memset(param_is_token, 0, PRODUCTION_MAX_LENGTH * sizeof(*param_is_token));
    parser_restore(parser, &chk);
  }

  return NULL; // no production in this rule accepts the current token
}

// track every token so it can be freed once, at the end of parsing
static bool parser_pool_push(struct Parser* parser, struct Token* tok) {
  if (parser->token_pool_size == parser->token_pool_cap) {
    size_t new_cap = parser->token_pool_cap == 0 ? 64 : parser->token_pool_cap * 2;
    struct Token** grown = realloc(parser->token_pool, new_cap * sizeof *grown);
    if (grown == NULL) return false;
    parser->token_pool = grown;
    parser->token_pool_cap = new_cap;
  }
  parser->token_pool[parser->token_pool_size++] = tok;
  return true;
}

static bool parser_move_forward(struct Parser* parser) {
  parser->current_token = malloc(sizeof(struct Token));
  memset(parser->current_token, 0, sizeof(struct Token)); // no garbage kind

  if (!parser_pool_push(parser, parser->current_token))
    return false; // out of memory

  while (true) {
    enum LexState lex_state = lexer_next_token(parser->lexer, parser->current_token);

    if (lex_state == LEX_ERROR || lex_state == LEX_PRG_ERROR) {
      sprintf(parser->error, "parsing failed: %s", parser->lexer->error);
      return false;
    }

    if (lex_state == LEX_EOF) {
      parser->current_token->kind = TOK_UNDEF;
      break;
    }

    if (parser->current_token->kind != TOK_SPACE) {
      break;
    }
  }

  return true;
}
