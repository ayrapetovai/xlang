#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec.h"
#include "lexer.h"
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
*/

int run_exec(char *filename) { return exec(filename); }

int run_lex(char *filename) {
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    return 1;
  }
  int error_code = 0;

  while (true) {
    Token t = {};
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF) break;
    else if (state == LEX_PRG_ERROR) {
      printf("lexer crashed\n");
      error_code = 1;
      break;
    } else if (state == LEX_ERROR) {
      printf("lexing: %s\n", lexer->error);
      error_code = 2;
      break;
    }
    print_token(&t);

    // skip commentaries
    switch (t.kind) {
    case TOK_SLC_START: lexer_skip_until(lexer, "\n"); break;
    case TOK_MLC_START: lexer_skip_until(lexer, "*/"); break;
    default:
    }
  }

  lexer_close(lexer);
  return error_code;
}

int run_ast(char *filename) {
  struct Lexer *lexer = lexer_new(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) return 2;
  if (strlen(parser->error) != 0) {
    printf("parsing error: %s\n", parser->error);
    return 3;
  }

  parser_parse(parser);

  return 0;
}

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("file name missed\n");
    return 1;
  }

  if (argc == 2) {
    char *filename = argv[1];
    return run_exec(filename);
  }

  if (!strcmp(argv[1], "run")) {
    char *filename = argv[2];
    return run_exec(filename);
  } else if (!strcmp(argv[1], "lex")) {
    char *filename = argv[2];
    return run_lex(filename);
  } else if (!strcmp(argv[1], "ast")) {
    char *filename = argv[2];
    return run_ast(filename);
  }

  return 1;
}
