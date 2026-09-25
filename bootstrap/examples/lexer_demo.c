/*
 * lexer_demo.c — how to use the lexer API from your own C program.
 *
 * Build:  cc -std=c17 -Wall -Wextra -Werror -I. -o lexer_demo lexer_demo.c lexer.c tokens.c
 * Run:    ./lexer_demo examples/hello.lang
 *
 * Two things every API consumer does, demonstrated here:
 *   1. loop over lexer_next() until T_EOF (or T_UNDEF);
 *   2. slice the token text via start/len and compare kinds.
 * The demo counts tokens per kind, reports every member access
 * (an identifier right after a '.' token) and every string literal,
 * and stops on a lexer error.
 */

#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0) { fclose(f); return NULL; }
  rewind(f);
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
  fclose(f);
  buf[n] = '\0';
  return buf;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: lexer_demo <file>\n");
    return 2;
  }
  char *src = read_file(argv[1]);
  if (!src) {
    fprintf(stderr, "lexer_demo: cannot read '%s'\n", argv[1]);
    return 2;
  }

  Lexer lx;
  lexer_init(&lx, src);

  size_t counts[T_KIND_COUNT] = {0};
  size_t total = 0;
  Token prev = {0};          /* one-token lookback, for member detection */
  int failed = 0;

  for (;;) {
    Token t = lexer_next(&lx);
    if (t.kind == T_UNDEF) {
      fprintf(stderr, "lex error: %s (line %d, col %d)\n", lx.err, t.line, t.col);
      failed = 1;
      break;
    }
    if (t.kind == T_EOF) break;

    counts[t.kind]++;
    total++;

    if (t.kind == T_ID && prev.kind == T_DOT)
      printf("member       %.*s\n", (int)t.len, t.start);
    if (t.kind == T_STR)
      printf("string       %.*s\n", (int)t.len, t.start);

    prev = t;
  }

  printf("\nTotal: %zu tokens\n", total);
  for (int k = 0; k < T_KIND_COUNT; k++)
    if (counts[k])
      printf("  %-8s %zu\n", token_kind_name((TokKind)k), counts[k]);

  free(src);
  return failed ? 1 : 0;
}