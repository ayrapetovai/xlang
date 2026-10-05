#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ast.h"
#include "exec.h"
#include "lexer.h"
#include "logger.h"
#include "parser.h"
#include "tokens.h"

/*
Bootstrap compiler does not support:
- coroutins (longjump?)
- UTF-8
- libraries
- compile-time code execution
- panicless output
- processing commentaries (skip only)
- string interpolation
- floating point numbers
- custom memory allocators
*/

int run_exec(char *filename) {
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    printf("parsing error: %s", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    printf("execution failed: %s", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 4;
  }

  parser_close(parser);
  lexer_close(lexer);

  int rc = exec(parse_result);
  node_free(parse_result);
  return rc;
}

int run_lex(char *filename) {
  LOG_DEBUG("minimal token size %db", sizeof(Token));
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    LOG_ERROR("lexer initialization: %s", lexer->error);
    lexer_close(lexer);
    return 1;
  }
  int error_code = 0;

  size_t token_count = 0;
  size_t value_bytes = 0;
  while (true) {
    Token *t = NULL;
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF) {
      free(t);
      break;
    } else if (state == LEX_ERROR) {
      free(t);
      LOG_ERROR("lexing error: %s", lexer->error);
      error_code = 2;
      break;
    }
    print_token(t);

    // skip commentaries
    switch (t->kind) {
    case TOK_SLC_START: lexer_skip_line(lexer); break;
    case TOK_MLC_START: lexer_skip_until(lexer, "*/"); break;
    default:
    }
    token_count++;
    value_bytes += strlen(t->value);
    free(t);
  }

  lexer_close(lexer);
  LOG_DEBUG("memory used for %d tokens %db", token_count, token_count * sizeof(Token) + value_bytes);
  return error_code;
}

int run_ast(char *filename) {
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    LOG_ERROR("lexer initialization: %s", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    LOG_ERROR("parsing error: %s", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    LOG_ERROR("ast failed: %s", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 4;
  }

  parser_close(parser);
  lexer_close(lexer);

  ast_dump(parse_result);
  node_free(parse_result);
  return 0;
}

int main(int argc, char **argv) {
  // logger_set_level(LOG_LEVEL_DEBUG);
  logger_set_level(LOG_LEVEL_INFO);

  LOG_DEBUG("program started");

  if (argc == 1) {
    LOG_ERROR("file name missed");
    return 1;
  }

  if (argc == 2) {
    char *filename = argv[1];
    return run_exec(filename);
  }

  int rc = 1;
  char *filename = argv[2];
  if (!strcmp(argv[1], "run")) {
    rc = run_exec(filename);
  } else if (!strcmp(argv[1], "lex")) {
    rc = run_lex(filename);
  } else if (!strcmp(argv[1], "ast")) {
    rc = run_ast(filename);
  }

  return rc;
}
