#ifndef LEXER_H
#define LEXER_H

#include "reader.h"
#include "tokens.h"

typedef struct Lexer {
  Reader *reader;
  int line;
  int col;
  char error[256];
} Lexer;

typedef enum LexState {
  LEX_EOF,
  LEX_OK,
  LEX_ERROR,
  LEX_UNDEF,
} LexState;

struct Lexer *lexer_new(char *);

enum LexState lexer_next_token(struct Lexer *, struct Token **);

// skips to ancor, consuming it
enum LexState lexer_skip_until(struct Lexer *, const char *);

// skips a single line comment body, leaving the terminating '\n' UNREAD so the
// statement separator survives
enum LexState lexer_skip_line(struct Lexer *);

void lexer_close(struct Lexer *);

#endif
