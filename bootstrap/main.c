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
*/

int run_exec(char *filename) { return exec(filename); }

int run_lex(char *filename) {
  struct Lexer *lexer = new_lexer(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    return 1;
  }
  int error_code = 0;

  while (true) {
    Token t = {};
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF)
      break;
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
    case TOK_SLC_START:
      lexer_skip_until(lexer, "\n");
      break;
    case TOK_MLC_START:
      lexer_skip_until(lexer, "*/");
      break;
    default:
      break;
    }
  }

  lexer_close(lexer);
  return error_code;
}

int run_ast(char *filename) {
  printf("ast %s\n", filename);
  struct Lexer *lexer = new_lexer(filename);
  if (strlen(lexer->error) != 0) {
    printf("lexer initialization: %s\n", lexer->error);
    return 1;
  }
  int ret = parse(lexer);
  lexer_close(lexer);

  return ret;
}

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("file name missed\n");
    exit(1);
  }

  if (argc == 2) {
    char *filename = argv[1];
    int r = run_exec(filename);
    exit(r);
  }

  if (!strcmp(argv[1], "run")) {
    char *filename = argv[2];
    int r = run_exec(filename);
    exit(r);
  } else if (!strcmp(argv[1], "lex")) {
    char *filename = argv[2];
    int r = run_lex(filename);
    exit(r);
  } else if (!strcmp(argv[1], "ast")) {
    char *filename = argv[2];
    int r = run_ast(filename);
    exit(r);
  }
}
