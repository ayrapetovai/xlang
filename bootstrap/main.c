#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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
    printf("lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    printf("parsing error: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    printf("execution failed: %s\n", parser->error);
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
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }
  int error_code = 0;

  while (true) {
    Token *t = NULL;
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF) {
      free(t); // the lexer allocates a TOK_UNDEF token to signal EOF
      break;
    } else if (state == LEX_PRG_ERROR) {
      free(t);
      printf("lexer crashed\n");
      error_code = 1;
      break;
    } else if (state == LEX_ERROR) {
      free(t);
      printf("lexing: %s\n", lexer->error);
      error_code = 2;
      break;
    }
    print_token(t);

    // skip commentaries
    switch (t->kind) {
    case TOK_SLC_START: lexer_skip_until(lexer, "\n"); break;
    case TOK_MLC_START: lexer_skip_until(lexer, "*/"); break;
    default:
    }
    free(t);
  }

  lexer_close(lexer);
  return error_code;
}

int run_ast(char *filename) {
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    printf("parsing error: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    printf("ast failed: %s\n", parser->error);
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
  logger_set_level(LOG_LEVEL_DEBUG);
  // logger_set_level(LOG_LEVEL_INFO);

  LOG_DEBUG("program started");

  if (argc == 1) {
    printf("file name missed\n");
    return 1;
  }

  if (argc == 2) {
    char *filename = argv[1];
    return run_exec(filename);
  }

  char *filename = argv[2];
  if (!strcmp(argv[1], "run")) {
    return run_exec(filename);
  } else if (!strcmp(argv[1], "lex")) {
    return run_lex(filename);
  } else if (!strcmp(argv[1], "ast")) {
    return run_ast(filename);
  }

  return 1;
}
