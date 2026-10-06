#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

#include "reader.h"
#include "tokens.h"

typedef struct LexStep {
  struct Token *tok;
  bool failed;
  int line;
  int col;
} LexStep;

typedef struct Lexer {
  Reader *reader;
  int line;
  int col;
  char error[256];
  struct LexStep *steps; // This log owns its tokens.
  size_t steps_len;
  size_t steps_cap;
  size_t steps_pos;

  bool at_eof; // the end-of-file token is already in the log
} Lexer;

typedef enum LexState {
  LEX_EOF,
  LEX_OK,
  LEX_ERROR,
  LEX_UNDEF,
} LexState;

struct Lexer *lexer_new(char *);

enum LexState lexer_next_token(struct Lexer *, struct Token **);

// skips to anchor, consuming it
enum LexState lexer_skip_until(struct Lexer *, const char *);

// Skips a all chars in the line starting from current position,
// leaving the terminating '\n' UNREAD
enum LexState lexer_skip_line(struct Lexer *);

// The next token the Parser should see. Skips spaces, comments.
enum LexState lexer_next_significant(struct Lexer *, struct Token **);

// Current position in the token log.
size_t lexer_mark(const struct Lexer *);

// Undo every token after `mark`, without touching the Reader.
void lexer_rewind_to(struct Lexer *, size_t mark);

void lexer_close(struct Lexer *);

#endif
