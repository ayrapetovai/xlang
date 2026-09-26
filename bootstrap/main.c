#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tokens.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"

int run_exec(char *filename) {
  printf("exec %s", filename);
  return 0;
}

int run_lex(char *filename) {
  printf("lex %s", filename);
  return 0;
}

int run_ast(char *filename) {
  printf("ast %s", filename);
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


