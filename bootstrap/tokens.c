#include "tokens.h"

#include <stdio.h>

void print_token(struct Token* tok) {
  printf("kind=%d, value=%s, line=%ld, col=%ld\n", tok->kind, tok->value, tok->line, tok->col);
}
