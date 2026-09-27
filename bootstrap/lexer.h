#ifndef LEXER_H
#define LEXER_H

#include "tokens.h"
#include "reader.h"

typedef struct Lexer {
  Reader* reader;
  char *current;
  int line;
  int col;
  char error[256];
} Lexer;

typedef enum LexState {
  LEX_EOF,
  LEX_OK,
  LEX_ERROR,
  LEX_UNDEF,
  LEX_PRG_ERROR,
} LexState;

struct Lexer* new_lexer(struct Reader*);

enum LexState lexer_next_token(struct Lexer*, struct Token*);

enum LexState lexer_skip_until(struct Lexer*, const char*);

void lexer_close(struct Lexer*);

#endif


