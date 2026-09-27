#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "tokens.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"
#include "reader.h"

/*
Bootstrap compiler does not support:
- coroutins
- UTF-8
- libraries
- compile-time code execution
- out.println is a direct call of C-printf
*/
int run_exec(char *filename) {
  printf("exec %s\n", filename);
  return 0;
}

int run_lex(char *filename) {
  struct Reader* reader = new_reader(filename);
  if (strlen(reader->error) != 0) {
    printf("reading error: %s\n", reader->error);
    return 1;
  }
  struct Lexer* lexer = new_lexer(reader);
  if (strlen(lexer->error) != 0) {
    printf("lexing error: %s\n", lexer->error);
    return 1;
  }
  int error_code = 0;

  while (true) {
    struct Token t = {};
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF) break;
    else if (state == LEX_PRG_ERROR) {
      printf("program error in lexer\n");
      error_code = 1;
      break;
    } else if (state == LEX_ERROR) {
      printf("lexing error: %s\n", lexer->error);
      error_code = 2;
      break;
    }
    print_token(&t);
  }

  lexer_close(lexer);
  return error_code;
}

int run_ast(char *filename) {
  printf("ast %s\n", filename);
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 1) {
    printf("file name missed\n");
    exit(1);
  }

  if (argc == 2) {
    char* filename = argv[1];
    int r = run_exec(filename);
    exit(r);
  }

  if (!strcmp(argv[1], "run")) {
    char* filename = argv[2];
    int r = run_exec(filename);
    exit(r);
  } else if (!strcmp(argv[1], "lex")) {
    char* filename = argv[2];
    int r = run_lex(filename);
    exit(r);
  } else if (!strcmp(argv[1], "ast")) {
    char* filename = argv[2];
    int r = run_ast(filename);
    exit(r);
  }
}


