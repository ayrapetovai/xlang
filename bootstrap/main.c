#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"

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

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: bootstrap <file>\n");
    return 2;
  }
  char *src = read_file(argv[1]);
  if (!src) {
    fprintf(stderr, "bootstrap: cannot read '%s'\n", argv[1]);
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