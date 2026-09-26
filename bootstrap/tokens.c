#include "tokens.h"

#include <stdio.h>

// must be reverse sorted or matching will read out of bounds
const TokenDef TOKEN_KEYWORDS[] = {
  { TOK_IMPORT, "import" },
  { TOK_IF    , "if"     },
  { TOK_NL    , "\n"     },
  { TOK_SPACE , " "      },
  { TOK_DOT   , "."      },
};
const size_t TOKEN_KEYWORDS_COUNT =
    sizeof TOKEN_KEYWORDS / sizeof TOKEN_KEYWORDS[0];

static const char* to_string(enum TokenKind token_kind) {
  for (size_t i = 0; i < TOKEN_KEYWORDS_COUNT; i++) {
    if (token_kind == TOK_NL)
      return "new line";
    else if (token_kind == TOK_SPACE)
      return "space";
    else if (token_kind == TOK_UNDEF)
      return "undefined";
    else if (token_kind == TOKEN_KEYWORDS[i].kind)
      return TOKEN_KEYWORDS[i].letters;
  }
  return "identifier";
}

void print_token(struct Token* tok) {
  printf("kind=%3d:%20s, value=%10s, line=%3ld, col=%3ld\n",
         tok->kind, to_string(tok->kind), tok->value, tok->line, tok->col);
}
