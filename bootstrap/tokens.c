#include "tokens.h"

#include <stdio.h>

// must be reverse sorted or matching will read out of bounds
const TokenDef TOKEN_KEYWORDS[] = {
  { TOK_INTERFACE, "interface" },
  { TOK_CONTINUE , "continue"  },
  { TOK_DEFAULT  , "default"   },
  { TOK_STRUCT   , "struct"    },
  { TOK_STRING   , "string"    },
  { TOK_SELECT   , "select"    },
  { TOK_RETURN   , "return"    },
  { TOK_MODULE   , "module"    },
  { TOK_IMPORT   , "import"    },
  { TOK_YIELD    , "yield"     },
  { TOK_WHILE    , "while"     },
  { TOK_SPAWN    , "spawn"     },
  { TOK_MATCH    , "match"     },
  { TOK_FLOAT    , "float"     },
  { TOK_FALSE    , "false"     },
  { TOK_ERROR    , "error"     },
  { TOK_DEFER    , "defer"     },
  { TOK_CONST    , "const"     },
  { TOK_CATCH    , "catch"     },
  { TOK_BYTES    , "bytes"     },
  { TOK_BREAK    , "break"     },
  { TOK_VOID     , "void"      },
  { TOK_UINT     , "uint"      },
  { TOK_TRUE     , "true"      },
  { TOK_THEN     , "then"      },
  { TOK_LOOP     , "loop"      },
  { TOK_FUNC     , "func"      },
  { TOK_ENUM     , "enum"      },
  { TOK_ELSE     , "else"      },
  { TOK_CHAR     , "char"      },
  { TOK_BOOL     , "bool"      },
  { TOK_TRY      , "try"       },
  { TOK_INT      , "int"       },
  { TOK_IS       , "is"        },
  { TOK_IN       , "in"        },
  { TOK_IF       , "if"        },
  { TOK_DO       , "do"        },

// separators
  { TOK_NL       , "\n"        },
  { TOK_SPACE    , " "         },
  { TOK_DOT      , "."         },
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
