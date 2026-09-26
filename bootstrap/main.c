#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "bootstrap: cannot open '%s'\n", path);
    exit(2);
  }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  rewind(f);
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t r = fread(buf, 1, (size_t)n, f);
  fclose(f);
  if (r != (size_t)n) { free(buf); return NULL; }
  buf[n] = '\0';
  return buf;
}

/* `bootstrap lex <file>` — token dump (default). */
static int run_lex(const char *path) {
  char *src = read_file(path);
  if (!src) {
    fprintf(stderr, "bootstrap: cannot read '%s'\n", path);
    return 2;
  }

  Lexer lx;
  lexer_init(&lx, src);

  for (;;) {
    Token t = lexer_next(&lx);
    printf("%-8s %4d:%-3d %.*s\n",
           token_kind_name(t.kind), t.line, t.col, (int)t.len, t.start);
    if (t.kind == T_UNDEF) {
      printf("  error: %s\n", lx.err);
      free(src);
      return 1;
    }
    if (t.kind == T_EOF) break;
  }
  free(src);
  return 0;
}

/* `bootstrap ast <file>` — lex the whole file, parse to an AST, dump it. */
static int run_ast(const char *path) {
  char *src = read_file(path);
  if (!src) {
    fprintf(stderr, "bootstrap: cannot read '%s'\n", path);
    return 2;
  }

  Parser p;
  Node *root = NULL;
  if (parser_run(&p, src, &root) != 0) {
    fprintf(stderr, "bootstrap: %s\n", p.msg);
    free(src);
    return 1;
  }
  node_dump(root, 0, stdout);
  arena_free(&p.ar);
  free(p.toks);
  free(src);
  return 0;
}

int main(int argc, char **argv) {
  const char *mode = "lex";
  const char *file = NULL;
  if (argc == 3 && strcmp(argv[1], "lex") == 0) {
    mode = "lex";
    file = argv[2];
  } else if (argc == 3 && strcmp(argv[1], "ast") == 0) {
    mode = "ast";
    file = argv[2];
  } else if (argc == 2) {
    mode = "lex";
    file = argv[1];
  } else {
    fprintf(stderr, "usage: bootstrap [lex|ast] <file>\n");
    return 2;
  }
  if (strcmp(mode, "ast") == 0) return run_ast(file);
  return run_lex(file);
}